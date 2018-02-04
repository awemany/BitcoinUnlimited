#include <map>
#include <unordered_map>
#include <queue>
#include "sync.h"
#include "weakblock.h"
#include "tweak.h"
#include "util.h"
#include "chainparams.h"

// FIXME: what about asserts here?

//#define DEBUG_DETAIL 1

// BU tweaks enable and config for weak blocks
extern CTweak<uint32_t> wbConsiderPOWratio;
extern CTweak<uint32_t> wbEnable;


// number of latest chain tips to keep after each purge
const int keep_chain_tips = 5;

bool weakblocksEnabled() {
    LOCK(cs_weakblocks);
    return wbEnable.value;
}

uint32_t weakblocksConsiderPOWRatio() {
    AssertLockHeld(cs_weakblocks);
    if (Consensus::Params().fPowNoRetargeting) {
        //LogPrint("weakblocks", "Returning consideration POW for testnet.\n");
        return 4;
    }
    //LogPrint("weakblocks", "Returning configured consideration POW ratio %d.\n", wbConsiderPOWratio.value);
    return wbConsiderPOWratio.value;
}

uint32_t weakblocksMinPOWRatio() {
    AssertLockHeld(cs_weakblocks);
    if (Consensus::Params().fPowNoRetargeting)
        return 8;
    return 600;
}


// Weak blocks data structures

// map from TXID back to weak blocks it is contained in
std::multimap<uint256, const Weakblock*> txid2weakblock;

// set of all weakly confirmed transactions
// this one uses most of the memory
std::map<uint256, CTransaction> weak_transactions;

// counts the number of weak blocks found per TXID
// is the number of weak block confirmations
// FIXME: maybe use an appropriate smart pointer structure here?
// Drawback would be to do all the ref counting where it doesn't really matters
std::map<uint256, size_t> weak_txid_refcount;

// map from block hash to weak block.
std::map<uint256, const Weakblock*> hash2weakblock;

// map from weak block memory location to hash
std::unordered_map<const Weakblock*, uint256> weakblock2hash;

// map from weakblock memory location to header info
std::unordered_map<const Weakblock*, CBlockHeader> weakblock2header;

// map weak blocks to the underlying block they minimally extend (or none)
// this, together with weak_chain_tips below, holds the weakblocks DAG
std::unordered_map<const Weakblock*, const Weakblock*> miniextends;

// weak/delta block chain tips
// Ordered chronologically - a later chain tip will be further down in the vector
// Therefore the "best weak block" is the one with the largest weak height that
// comes earliest in this vector.
std::vector<const Weakblock*> weak_chain_tips;

// Cache of blocks reassembled from weakblocks
std::unordered_map<const Weakblock*, const CBlock*> reassembled;

CCriticalSection cs_weakblocks;

// Tests whether wb is extending 'underlying', which is:
// wb strictly more transactions than underlying
// except for coinbase (txn #0), all transactions of underlying are
// in wb and in the same order
// this is the transitive partial order "<" of which
// the covering relation is the 'mini extends' one: "<:"
bool weakExtends(const Weakblock* under, const Weakblock* wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb == under) return false;
    if (wb->size() <= under->size()) return false;
    for (size_t i=1; i < under->size(); i++)
        if ((*wb)[i] != (*under)[i]) return false;
    return true;
}


// Helper function to insert a transaction into the weak_transactions list
// and do ref counting.
static inline CTransaction* storeTransaction(const CTransaction &otx) {
    AssertLockHeld(cs_weakblocks);
    uint256 txid = otx.GetHash();
    CTransaction *tx = NULL;
    if (weak_transactions.count(txid) != 0) {
        tx = &weak_transactions[txid];
        weak_txid_refcount[txid]++;
    } else {
        assert (weak_txid_refcount.count(txid) == 0);
        weak_transactions[otx.GetHash()] = otx;
        tx = &weak_transactions[txid];
        weak_txid_refcount[txid]=1;
    }
    return tx;
}

// ordering relation used for the priority queue in insertChainDAG(..)
struct compareByHeight {
    bool operator()(const Weakblock* left, const Weakblock* right) {
        return weakHeight(left)<weakHeight(right);
    }
};

static void reconnectNodes(const Weakblock* candidate, const Weakblock* wb) {
    // Check all the blocks that miniextended candidate before.
    // They might miniextend wb now and might need to move to larger block height.
    // FIXME: It might make sense to create a datastructure for the inverse
    // of miniextends to use here to make this more efficient.
    bool buried=false;
    for (std::pair<const Weakblock*, uint256> p : weakblock2hash) {
        const Weakblock* t = p.first;

        if ((miniextends.count(t) > 0) != (candidate == NULL)) {
            bool attaches = candidate == NULL;
            if (!attaches) attaches = miniextends[t] == candidate;
            if (attaches && weakExtends(wb, t)) {
                if (candidate == NULL)
                    LogPrint("weakblocks", "Weakblock %s was root before before. Now is mini-extending %s.\n",
                             weakblock2hash[t].GetHex(), weakblock2hash[wb].GetHex());
                else
                    LogPrint("weakblocks", "Weakblock %s mini-extended %s before. Now is mini-extending %s.\n",
                             weakblock2hash[t].GetHex(), weakblock2hash[candidate].GetHex(), weakblock2hash[wb].GetHex());

                assert (t != NULL);
                assert (wb != NULL);
                miniextends[t] = wb;
                buried = true;
            }
        }
    }
    if (! buried) {
        LogPrint("weakblocks", "Block is not buried and thus a new chain tip.\n");

        auto wct_iter = find(weak_chain_tips.begin(),
             weak_chain_tips.end(),
             candidate);
        if (wct_iter!= weak_chain_tips.end()) {
            LogPrint("weakblocks", "Removing/replacing old chain tip %s.\n", weakblock2hash[candidate].GetHex());
            weak_chain_tips.erase(wct_iter);
        }
        weak_chain_tips.push_back(wb);
    }
}

// update mini_extends for a new weak block wb - which is not in the current DAG
static void insertChainDAG(Weakblock* wb) {
    AssertLockHeld(cs_weakblocks);
    assert (miniextends.count(wb) == 0);

    // Priority queue sorted by weakHeight - try longest chains first
    std::priority_queue<const Weakblock*, std::vector<const Weakblock*>, compareByHeight> to_check;

    for (auto tip : weak_chain_tips)
        to_check.push(tip);

    int check_iteration = 0;
    while (to_check.size()) {
        LogPrint("weakblocks", "Checking %d chain tips at iteration %d.\n", to_check.size(), check_iteration);
        const Weakblock* candidate = to_check.top(); to_check.pop();

        // insert next to check into queue, if available
        if (miniextends.count(candidate))
            to_check.push(miniextends[candidate]);

        LogPrint("weakblocks", "Checking whether weakblock %s extends %s?\n", weakblock2hash[wb].GetHex(), weakblock2hash[candidate].GetHex());
        if (weakExtends(candidate, wb)) {
            LogPrint("weakblocks", "Weakblock %s extends %s.\n", weakblock2hash[wb].GetHex(), weakblock2hash[candidate].GetHex());
            assert (candidate != NULL);
            assert (wb != NULL);
            miniextends[wb] = candidate;

            reconnectNodes(candidate, wb);
            return;
        }
        check_iteration++;
    }
    LogPrint("weakblocks", "Weakblock %s does not extend any previous weak block. Inserting as new chain tip and potentially stacking other chains on top.\n",
             weakblock2hash[wb].GetHex());
    reconnectNodes(NULL, wb);
}

bool storeWeakblock(const CBlock &block) {
    uint256 blockhash = block.GetHash();
    Weakblock* wb=new Weakblock();


    LOCK(cs_weakblocks);
    if (hash2weakblock.count(blockhash) > 0) {
        // stored it already
        return false;
    }
    for (const CTransaction& otx : block.vtx) {
        CTransaction *tx = storeTransaction(otx);
        uint256 txhash = tx->GetHash();
        txid2weakblock.insert(std::pair<uint256, const Weakblock*>(txhash, wb));
        wb->push_back(tx);
    }

    hash2weakblock[blockhash] = wb;
    weakblock2hash[wb] = blockhash;
    weakblock2header[wb] = block;

    insertChainDAG(wb);
    LogPrint("weakblocks", "Tracking weak block %s of %d transactions.\n", blockhash.GetHex(), wb->size());
    return true;
}

/*! Reassemble a block from a weak block. Does NOT check the
  reassembled array for a cached result first; that is the purpose of
  the blockForWeak(..) accessor. */
static inline const CBlock* reassembleFromWeak(const Weakblock* wb) {
    AssertLockHeld(cs_weakblocks);
    assert(wb != NULL);

    CBlock* result = new CBlock(weakblock2header[wb]);
    for (CTransaction* tx : *wb) {
        result->vtx.push_back(*tx);
    }
    assert (weakblock2hash[wb] == result->GetHash());
    return result;
}

const CBlock* blockForWeak(const Weakblock* wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb == NULL) return NULL;
    if (reassembled.count(wb) == 0)
        reassembled[wb] = reassembleFromWeak(wb);
    return reassembled[wb];
}

const Weakblock* getWeakblock(const uint256& blockhash) {
    AssertLockHeld(cs_weakblocks);
    if (hash2weakblock.count(blockhash))
        return hash2weakblock[blockhash];
    else return NULL;
}

const uint256 HashForWeak(const Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb == NULL) return uint256();
    return weakblock2hash[wb];
}

int weakHeight(const Weakblock* wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb==NULL) {
        LogPrint("weakblocks", "weakHeight(NULL) == -1\n");
        return -1;
    }
    //LogPrint("weakblocks", "weakHeight(..), checking: %p %s\n", wb, weakblock2hash[wb].GetHex());
    if (miniextends.count(wb))
        return 1+weakHeight(miniextends[wb]);
    else
        return 0;
}

const Weakblock* getWeakLongestChainTip() {
    LOCK(cs_weakblocks);
    int max_height=-1;
    const Weakblock* longest = NULL;

    for (const Weakblock* wb : weak_chain_tips) {
        int height = weakHeight(wb);
        if (height > max_height) {
            longest = wb;
            max_height = height;
        }
    }
    return longest;
}

// opposite of storeTransaction: remove a transaction from weak_transactions and
// do ref-counting.
static inline void removeTransaction(const CTransaction *tx) {
    AssertLockHeld(cs_weakblocks);
    uint256  txhash=tx->GetHash();
    assert (weak_txid_refcount[txhash] > 0);
    assert (weak_transactions.count(txhash) > 0);
    weak_txid_refcount[txhash]--;

    if (weak_txid_refcount[txhash] == 0) {
        weak_transactions.erase(txhash);
        weak_txid_refcount.erase(txhash);
        txid2weakblock.erase(txhash);
    }
}

// Forget about a weak block. Cares about the immediate indices and the transaction list
// but NOT the DAG in mini_extends / weak_chain_tips.
static inline void forgetWeakblock(Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    LogPrint("weakblocks", "Removing weakblock %s.\n", weakblock2hash[wb].GetHex());
    // map from TXID back to weak blocks it is contained in
    uint256 wbhash = weakblock2hash[wb];

    for (CTransaction* tx : *wb) {
        removeTransaction(tx);
    }
    hash2weakblock.erase(wbhash);
    weakblock2hash.erase(wb);
    weakblock2header.erase(wb);
    if (reassembled.count(wb) > 0)
        reassembled.erase(wb);
    delete wb;
}

/* Remove a weak block chain tip and all blocks before that one that are not part of other known chains. */
static inline void purgeChainTip(Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    LogPrint("weakblocks", "Purging weak block %s, which is currently a chain tip.\n", weakblock2hash[wb].GetHex());
    auto wct_iter = find(weak_chain_tips.begin(),
                         weak_chain_tips.end(),
                         wb);
    assert(wct_iter != weak_chain_tips.end());
    weak_chain_tips.erase(wct_iter);

    Weakblock* wb_old;

    do {
        forgetWeakblock(wb);
        wb_old = NULL;

        if (miniextends.count(wb)) {
            wb_old = const_cast<Weakblock*>(wb);
            wb = const_cast<Weakblock*>(miniextends[wb_old]);
            miniextends.erase(wb_old);

            // stop if any other chain depends on wb now
            for (std::pair<const Weakblock*, uint256> p : weakblock2hash) {
                const Weakblock* other = p.first;
                if (miniextends.count(other)) {
                    if (miniextends[other] == wb) {
                        LogPrint("weakblocks", "Stopping removal at %s as it is used by other chain block %s.\n",
                                 weakblock2hash[wb].GetHex(), weakblock2hash[other].GetHex());
                        return;
                    }
                }
            }
        }
    } while (wb_old != NULL);
    LogPrint("weakblocks", "Purge finished, reached bottom of chain.\n");
}

void purgeOldWeakblocks(int leave_tips) {
    LOCK(cs_weakblocks);
    LogPrint("weakblocks", "Purging old chain tips. %d chain tips right now.\n", weak_chain_tips.size());

    if (leave_tips<0) leave_tips = keep_chain_tips;
    while ((int)weak_chain_tips.size() > leave_tips) {
        purgeChainTip(const_cast<Weakblock*>(weak_chain_tips[0]));
    }
}

std::vector<std::pair<uint256, size_t> > weakChainTips() {
    LOCK(cs_weakblocks);
    std::vector<std::pair<uint256, size_t> > result;
    for (const Weakblock* wb : weak_chain_tips)
        result.push_back(std::pair<uint256, size_t>(weakblock2hash[wb],
                                                    weakHeight(wb)));
    return result;
}

const Weakblock* miniextendsWeak(const Weakblock *block) {
    AssertLockHeld(cs_weakblocks);
    if (miniextends.count(block))
        return miniextends[block];
    else return NULL;
}

int numKnownWeakblocks() { LOCK(cs_weakblocks); return weakblock2hash.size(); }
int numKnownWeakblockTransactions() { LOCK(cs_weakblocks); return weak_transactions.size(); }


void weakblocksConsistencyCheck() {
    LOCK(cs_weakblocks);
    LogPrint("weakblocks", "Doing internal consistency check.\n");
    assert(hash2weakblock.count(uint256()) == 0);
    assert(weakblock2header.count(NULL) == 0);
    assert(weakblock2hash.count(NULL) == 0);
    assert(hash2weakblock.size() == weakblock2hash.size());
    assert(weakblock2header.size() == hash2weakblock.size());
    assert(weak_chain_tips.size() <= hash2weakblock.size());
    int longest_height=-1;
    std::set<const Weakblock*> longest_tips;

    for (std::pair<uint256, const Weakblock*> p : hash2weakblock) {
        const uint256 blockhash = p.first;
        const Weakblock* wb = p.second;

        LogPrint("weakblocks", "Consistency check for weak block %s.\n", blockhash.GetHex());

        assert(weakblock2hash[wb] == blockhash);

        // collect chain of blocks this one builds upon
        std::set<const Weakblock*> chain;
        const Weakblock* extends = wb;

        while (miniextendsWeak(extends) != NULL) {
            extends = miniextendsWeak(extends);
            chain.insert(extends);
            assert(weakExtends(extends, wb));
        }
        LogPrint("weakblocks", "Chain size: %d, weak height: %d\n", chain.size(), weakHeight(wb));
        assert ((int)chain.size() == weakHeight(wb));

        if ((int)chain.size() >= longest_height) {
            if ((int)chain.size() > longest_height) {
                longest_tips.clear();
            }
            longest_tips.insert(wb);
            longest_height = chain.size();
        }

        for (std::pair<uint256, const Weakblock*> pother : hash2weakblock) {
            //const uint256 otherhash = pother.first;
            const Weakblock* wother = pother.second;

            LogPrint("weakblocks", "Potentially testing that %s is not underlying %s (%d).\n", weakblock2hash[wother].GetHex(), weakblock2hash[wb].GetHex(), chain.count(wother));
            if (! chain.count(wother)) {
                assert(!weakExtends(wother, wb));
            }
        }
    }

    if (longest_height < 0) {
        assert(getWeakLongestChainTip() == NULL);
    } else {
        assert(longest_tips.count(getWeakLongestChainTip()));
    }
}

void weakblocksEmptyCheck() {
    LOCK(cs_weakblocks);
    assert (txid2weakblock.size() == 0);
    assert (weak_transactions.size() == 0);
    assert (weak_txid_refcount.size() == 0);
    assert (hash2weakblock.size() == 0);
    assert (weakblock2hash.size() == 0);
    assert (weakblock2header.size() == 0);
    assert (miniextends.size() == 0);
    assert (weak_chain_tips.size() == 0);
    assert (reassembled.size() == 0);
}
