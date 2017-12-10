#include "weakblock.h"

// Weak blocks data structures

std::multimap<uint256, CBlock*> txid2weakblock;
std::map<uint256, CBlock*> hash2weakblock;
std::vector<CBlock*> weakblocks;
CCriticalSection cs_weakblocks;
