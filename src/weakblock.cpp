#include "sync.h"
#include "weakblock.h"
#include "tweak.h"
#include "chainparams.h"


// BU tweaks enable and config for weak blocks
extern CTweak<uint32_t> wbConsiderPOWratio;
extern CTweak<uint32_t> wbEnable;

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

// FIXME: use unordered_map wherever it makes sense!

// counts the number of weak blocks found per TXID
// is the number of weak block confirmations
std::map<uint256, size_t> weak_confirmations;

// set of all weakly confirmed transactions
// this one uses all the memory
std::map<uint256, CTransaction> weak_transactions;

// map from block hash to weak block.
std::map<uint256, Weakblock*> hash2weakblock;

// map from weak block pointer to hash
std::map<const Weakblock*, uint256> weakblock2hash;

std::map<const Weakblock*, CBlockHeader> weakblock2header;

// Chronologically sorted weak blocks
std::vector<Weakblock*> weakblocks;

// last returned CBlock
CBlock *last_req_block=NULL;

CCriticalSection cs_weakblocks;

static inline CBlock* reassembleFromWeak(const Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    assert(wb != NULL);

    CBlock* result = new CBlock(weakblock2header[wb]);
    for (CTransaction* tx : *wb) {
        result->vtx.push_back(*tx);
    }
    assert (weakblock2hash[wb] == result->GetHash());
    return result;
}

bool storeWeakblock(const CBlock &block) {
    uint256 blockhash = block.GetHash();
    LOCK(cs_weakblocks);
    if (hash2weakblock.count(blockhash) == 0) {
        Weakblock* wb=new Weakblock();
        for (const CTransaction& otx : block.vtx) {
            uint256 txid = otx.GetHash();
            CTransaction *tx;
            if (weak_transactions.count(txid) != 0) {
                tx = &weak_transactions[txid];
                weak_confirmations[txid]++;
            } else {
                weak_transactions[otx.GetHash()] = otx;
                tx = &weak_transactions[txid];
                weak_confirmations[txid]=1;
            }
            wb->push_back(tx);
        }
        hash2weakblock[blockhash] = wb;
        weakblock2hash[wb] = blockhash;
        weakblocks.push_back(wb);
        weakblock2header[wb] = block;
        LogPrint("weakblocks", "Tracking weak block of %d transactions.\n", wb->size());
        return true;
    } else return false;
}

const Weakblock* getWeakblock(const uint256& blockhash) {
    AssertLockHeld(cs_weakblocks);
    if (hash2weakblock.count(blockhash))
        return hash2weakblock[blockhash];
    else return NULL;
}

const CBlock* blockForWeak(const Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb == NULL) return NULL;
    else if (last_req_block != NULL && last_req_block->GetHash() == HashForWeak(wb)) {
        return last_req_block;
    } else {
        if (last_req_block != NULL)
            delete last_req_block;
        last_req_block = reassembleFromWeak(wb);
        return last_req_block;
    }
}

void resetWeakblocks() {
    LOCK(cs_weakblocks);
    for (Weakblock* wb : weakblocks)
        delete wb;
    weakblocks.clear();
    hash2weakblock.clear();
    weakblock2hash.clear();
    weak_transactions.clear();
    weak_confirmations.clear();
    weakblock2header.clear();
    if (last_req_block != NULL)
        delete last_req_block;
    last_req_block = NULL;
}

std::vector<std::pair<uint256, size_t> > weakStats() {
    LOCK(cs_weakblocks);
    std::vector<std::pair<uint256, size_t> > result;
    for (Weakblock* wb : weakblocks)
        result.push_back(std::pair<uint256, size_t>(weakblock2hash[wb],
                                                    wb->size()));
    return result;
}

const Weakblock* buildsOnWeak(const CBlock &block) {
    AssertLockHeld(cs_weakblocks);
    LogPrint("weakblocks", "Check whether block %s is delta on top of last weak block coming before it.\n", block.GetHash().GetHex());

    if (!weakblocks.size()) {
        LogPrint("weakblocks", "Currently no weak blocks -> Nope.\n");
        return NULL;
    }

    if (block.vtx.size() < 2) {
        LogPrint("weakblocks", "Coinbase-transaction-only block -> Nope.\n");
        return NULL;
    }

    int result = weakblocks.size()-1;

    // stack weak blocks only onto blocks received earlier than the one at hand, and
    // also do not stack a block onto itself.
    // The reason to do this loop instead of just comparing the last and second last block is the following scenario:
    // WB1 arrives
    // WB2 arrives with same transaction set as WB1
    // WB1 might be built on top of WB2, confusing peers
    for (size_t i=1; i < weakblocks.size(); i++) {
        if (weakblock2hash[weakblocks[i]] == block.GetHash()) {
            result = i-1;
            LogPrint("weakblocks", "Block is identical to weak block %d (out of %d weakblocks) - trying to stack onto the one before.\n", i, weakblocks.size());
            break;
        }
    }


    Weakblock* underlying = weakblocks[result];
    uint256 underlying_hash = weakblock2hash[underlying];

    if (underlying_hash == block.GetHash()) {
        LogPrint("weakblocks", "No matching block other than itself.\n");
        return NULL;
    }

    if (block.vtx.size() < underlying->size()) {
        LogPrint("weakblocks", "New block is smaller than latest weak block -> Nope.\n");
        return NULL;
    }

    // all except coinbase of the underlying must be included in the new block
    for (size_t i = 1; i < underlying->size(); i++) {
        if (block.vtx[i].GetHash() != (*underlying)[i]->GetHash()) {
            LogPrint("weakblocks", "New block and latest weakblock differ at pos %d, new: %s, tested weak: %s\n",
                     i, block.vtx[i].GetHash().GetHex(), underlying_hash.GetHex());
            return NULL;
        }
    }
    LogPrint("weakblocks", "Yes, this block is containing all of the weak block's (%s:%d (out of %d weakblocks)) transactions and in the same order.\n",
             HashForWeak(weakblocks[result]).GetHex(),
             result, weakblocks.size());
    return weakblocks[result];
}

size_t weakConfirmations(const uint256& txid) {
    LOCK(cs_weakblocks);
    if (weak_confirmations.count(txid) > 0)
        return weak_confirmations[txid];
    else
        return 0;
}

const uint256 HashForWeak(const Weakblock *wb) {
    AssertLockHeld(cs_weakblocks);
    if (wb == NULL) return uint256();
    return weakblock2hash[wb];
}
