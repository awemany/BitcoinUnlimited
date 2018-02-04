// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"
#include "utilstrencodings.h"
#include "weakblock.h"
#include <univalue.h>

using namespace std;

UniValue weakstats(const UniValue& params, bool fHelp) {
    throw runtime_error("Disabled for now. FIXME\n");

    if (fHelp || params.size() != 0)
        throw runtime_error(
            "weakstats\n"
            "\nReturns weak block statistics.\n");

    // FIXME: make the results nicer and more informative
    UniValue result(UniValue::VOBJ);
/*
    std::vector<std::pair<uint256, size_t> > wstats = weakStats();

    // order by receival time
    for (std::pair<uint256, size_t> p : wstats)
        result.push_back(Pair(p.first.GetHex(), p.second));

        return result;*/
}

UniValue weakconfirmations(const UniValue& params, bool fHelp)
{
    throw runtime_error("Disabled for now. FIXME\n");
    if (fHelp || params.size() < 1)
        throw runtime_error(
            "weakconfirmations \"hexstring\"\n"
            "\nGives the number of weak block confirmation a transaction (refered to by txid) has received.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the TXID\n"
            "\nResult:\n"
            "\"num\"             (int) The number of weak block confirmations\n");


    std::string txid_hex = params[0].get_str();

    uint256 hash = ParseHashV(params[0], "parameter 1");
    //return weakConfirmations(hash);
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "weakblocks",         "weakconfirmations",      &weakconfirmations,      true  },
    { "weakblocks",         "weakstats",              &weakstats,   true  },
};

void RegisterWeakBlockRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
