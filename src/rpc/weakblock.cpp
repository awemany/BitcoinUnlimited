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
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "weakstats\n"
            "\nReturns various weak block statistics.\n");

    UniValue result(UniValue::VOBJ);

    LOCK(cs_weakblocks);
    result.push_back(Pair("numknownweakblocks", numKnownWeakblocks()));
    result.push_back(Pair("numknownweakblocktransactions", numKnownWeakblockTransactions()));
    result.push_back(Pair("numweakchaintips", weakChainTips().size()));
    result.push_back(Pair("maxweakchainheight", weakHeight(getWeakLongestChainTip())));
    return result;
}

UniValue weakchaintips(const UniValue &params, bool fHelp) {
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "weakchaintips\n"
            "\nGives back the current weak chain tips as pairs of (weak block hash, weak chain height), in chronological order\n");

    std::vector<std::pair<uint256, int> > wct_heights = weakChainTips();
    UniValue result(UniValue::VARR);

    for (std::pair<uint256, int> p : wct_heights) {
        UniValue entry(UniValue::VARR);
        entry.push_back(p.first.GetHex());
        entry.push_back(p.second);
        result.push_back(entry);
    }
    return result;
}

UniValue weaktiptxcount(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "weaktiptxcount\n"
            "\nGives the number of transactions in the longest weak chain tip. Returns -1 if no weak chain tip is available.\n");

    LOCK(cs_weakblocks);
    const Weakblock* wbtip = getWeakLongestChainTip();
    if (wbtip == NULL)
        return -1;
    else {
        return wbtip->size();
    }
}

UniValue weakconfirmations(const UniValue& params, bool fHelp)
{
    throw runtime_error("Disabled for now. FIXME\n");
    /*
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
    //return weakConfirmations(hash); */
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "weakblocks",         "weakconfirmations",      &weakconfirmations,      true  },
    { "weakblocks",         "weakstats",              &weakstats,   true  },
    { "weakblocks",         "weakchaintips",          &weakchaintips, true },
    { "weakblocks",         "weaktiptxcount",         &weaktiptxcount, true },
};

void RegisterWeakBlockRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
