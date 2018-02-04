// Copyright (c) 2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEAKBLOCK_H
#define BITCOIN_WEAKBLOCK_H

#include "uint256.h"
#include "consensus/params.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "sync.h"

const uint32_t DEFAULT_WEAKBLOCKS_CONSIDER_POW_RATIO=30;
const bool DEFAULT_WEAKBLOCKS_ENABLE=true;

bool weakblocksEnabled();
uint32_t weakblocksConsiderPOWRatio();

// absolute minimum POW target multiplicator - below this, also weak blocks are considered invalid and nodes sending those are penalized and banned
uint32_t weakblocksMinPOWRatio();

//! protects all of the above data structures
extern CCriticalSection cs_weakblocks;

// Internally, a weak block is just a bunch of pointers, to save
// memory compared to storing all transactions as duplicates for
// each weak block
typedef std::vector<CTransaction*> Weakblock;

bool weakExtends(const Weakblock* under, const Weakblock* wb);

// store CBlock as a weak block return  iff the block was stored, false if it already exists
bool storeWeakblock(const CBlock &block);

// return pointer to a CBlock if a given hash is a stored, or NULL
// returned block needs to be handled with cs_weakblocks locked
// responsibility of memory management is internal to weakblocks module
// the returned block is valid until the next call to purgeOldWeakblocks
const CBlock* blockForWeak(const Weakblock *wb);

// return a weak block. Caller needs to care for cs_weakblocks
const Weakblock* getWeakblock(const uint256& hash);

// give hash of a weak block. Needs to be cs_weakblocks locked
const uint256 HashForWeak(const Weakblock* wb);

// convenience function around getWeakblock
inline bool isKnownWeakblock(const uint256& hash) {
    AssertLockHeld(cs_weakblocks);
    return getWeakblock(hash) != NULL;
}

/*! Return the weak height of a weakblock
  The height is the number of weak blocks that come before this one.
Needs to be called with cs_weakblocks locked. */
int weakHeight(const Weakblock*);

/*! Return block from longest and earliest weak chain
  Can return NULL if there is no weak block chain available. */
const Weakblock* getWeakLongestChainTip();

// remove old weak blocks after a while and leave only the given number of chaintips (default is used if -1 is given)
void purgeOldWeakblocks(int leave_tips = -1);

// return a map of weak block hashes to their weak block height, in chronological order of receival
std::vector<std::pair<uint256, size_t> > weakChainTips();

// return block minimally extending the given weak block (or NULL)
// This needs to be handled with cs_weakblocks locked
const Weakblock* miniextendsWeak(const Weakblock *block);

// currently known number of weak blocks
int numKnownWeakblocks();

// currently known number of transactions appearing in weak blocks
int numKnownWeakblockTransactions();

//! Internal consistency check
/*! To be used only for testing / debugging.
  For each weak block that is registered, this checks that:
  - hash2weakblock and weakblock2hash are consistent
  - it miniextends the block that miniextends says it does.
  - it extends only blocks that can be reached through the miniextends DAG

  It also checks that getWeakLongestChainTip() is indeed pointing to one of
  the longest chains of weakblocks.

  Runtime is O(<number-of-weak-blocks>^2)
*/
void weakblocksConsistencyCheck();

//! Consistency check that all internal data structures are empty
void weakblocksEmptyCheck();

#endif
