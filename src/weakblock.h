// Copyright (c) 2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEAKBLOCK_H
#define BITCOIN_WEAKBLOCK_H

#include <map>
#include "uint256.h"
#include "consensus/params.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "sync.h"

// FIXME: Weakblocks and mempool?
// FIXME: Weakblocks and mempool and getblocktemplate?

const uint32_t DEFAULT_WEAKBLOCKS_CONSIDER_POW_RATIO=30;
const bool DEFAULT_WEAKBLOCKS_ENABLE=true;

bool weakblocksEnabled();
uint32_t weakblocksConsiderPOWRatio();

// absolute minimum POW target multiplicator - below this, also weak blocks are considered invalid and nodes sending those are penalized and banned
uint32_t weakblocksMinPOWRatio();

// Weak blocks data structures
//! Map TXID to weak blocks containing them
//extern std::multimap<uint256, CBlock*> txid2weakblock;
//! The received weak blocks since the last strong one, indexed by their hash
//extern std::map<uint256, CBlock*> hash2weakblock;
//! Weak blocks in order of receival
//extern std::vector<CBlock*> weakblocks;

//! protects all of the above data structures
extern CCriticalSection cs_weakblocks;

// Internally, a weak block is just a bunch of pointers, to save
// memory compared to storing all transactions as duplicates for
// each weak block
typedef std::vector<CTransaction*> Weakblock;

// store a weak block return true iff the block was stored, false if it already exists
bool storeWeakblock(const CBlock &block);

// return pointer to a CBlock if a given hash is a stored, or NULL
// returned block needs to be handled with cs_weakblocks locked
// responsibility of memory management is internal to weakblocks module
// the returned block is valid until the next call to getWeakblock
const CBlock* blockForWeak(const Weakblock *wb);


// return a weak block. Caller needs to care for cs_weakblocks
const Weakblock* getWeakblock(const uint256& hash);


// convenience function around getWeakblock
inline bool isKnownWeakblock(const uint256& hash) {
    AssertLockHeld(cs_weakblocks);
    return getWeakblock(hash) != NULL;
}

// remove all weak blocks (in case strong block came in)
void resetWeakblocks();

// return a map of weak block hashes to the number of confirmations contained therein, in chronological order of receival
std::vector<std::pair<uint256, size_t> > weakStats();

// Test whether a given block builds on top of an available weak block,
// if so, return a pointer to that block. Else return NULL.
// (Building on top means: All transactions in a weak block are contained
// in the given block, in the same order, except for the coinbase transaction)
// FIXME: This operation is currently O(<#txn>)
// This needs to be handled with cs_weakblocks locked
const Weakblock* buildsOnWeak(const CBlock &block);

// give the number of weak blocks a transaction is in
size_t weakConfirmations(const uint256& txid);

// give hash of a weak block. Needs to be cs_weakblocks locked
const uint256 HashForWeak(const Weakblock* wb);
#endif
