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

// Weak blocks data structures
//! Map TXID to weak blocks containing them
extern std::multimap<uint256, CBlock*> txid2weakblock;
//! The received weak blocks since the last strong one, indexed by their hash
extern std::map<uint256, CBlock*> hash2weakblock;
//! Weak blocks in order of receival
extern std::vector<CBlock*> weakblocks;

//! protects all of the above data structures
extern CCriticalSection cs_weakblocks; 


#endif
