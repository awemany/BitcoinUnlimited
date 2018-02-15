#!/usr/bin/env python3
# unit tests for the weakblocks subsystem
# This needs some serious clean-up
import random
from weakblock import *
import primitives
import coreio
import script
import util

# switch on debugging output to console
util.cvar.fPrintToConsole = True
util.cvar.fDebug = False
util.cvar.mapMultiArgs["-debug"]=util.strVector(["1"])

random.seed(1)

# if running repeatedly, interactively:
weakblocksConsistencyCheck()
purgeOldWeakblocks(0)
weakblocksEmptyCheck()

# Important note:
# transactions return an uint256 stored inside them as their
# hash, which means that if they are destroyed, the GetHash()
# result obtained becomes invalid. This is reason for funny
# results if you do something like
# print(randomTxn().GetHash().GetHex())

def randhex():
    """ Pseudo-random hex string of 64 nibbles (256 bit) """
    n = random.randint(0, 1<<256 -1)
    h = hex(n)[2:].zfill(64)
    return h

def rand256():
    """ Pseudo-random uint256. """
    return coreio.ParseHashStr(randhex(), "ranhex")

def randomTxn():
    """ generate 'transaction' with pseudo-random garbage data """
    txn = primitives.CTransaction()
    h = randhex()
    tx_ver = "01000000"
    tx_incount = "01"
    tx_outcount = "00"
    tx_in = h
    tx_in_outindex = "00000000"
    tx_in_script_len = "01"
    tx_in_script = "55"
    tx_in_seqno = "ffffffff"

    tx_input = tx_in + tx_in_outindex + tx_in_script_len + tx_in_script + tx_in_seqno

    tx_out_val = "0100000000000000"
    tx_out_script_len = "01"
    tx_out_script = "aa"

    tx_output = tx_out_val + tx_out_script_len + tx_out_script

    tx_hex = tx_ver + tx_incount + tx_input + tx_outcount + tx_output

    assert coreio.DecodeHexTx(txn, tx_hex)
    return txn

def randomBlock(underlying=None, num_txn=10):
    """ generate fake block with fake header and fake pseudo-random hash (which does NOT match its contents!). If underlying isn't None, create first (coinbase) transaction randomly, fill with transactions from underlying and then put num_txn on top. """
    block = primitives.CBlock()
    block.hashMerkleRoot = rand256()
    assert num_txn > 0

    block.vtx.push_back(randomTxn())

    if underlying is not None:
        for tx in underlying.vtx[1:]:
            block.vtx.push_back(tx)

    for _ in range(num_txn):
        block.vtx.push_back(randomTxn())
    return block


assert weakblocksEnabled()
assert weakblocksConsiderPOWRatio() <= weakblocksMinPOWRatio()


null256 = primitives.uint256()

# test NULL arguments
assert blockForWeak(None) is None
assert getWeakblock(null256) is None
assert primitives.Equals(HashForWeak(None), null256)
assert not isKnownWeakblock(null256)
assert weakHeight(None) == -1

assert getWeakLongestChainTip() is None
assert miniextendsWeak(None) is None
weakblocksConsistencyCheck()

# weak block test tree scenarios (list of CBlocks that should
# produce a weakblocks tree)
def scenario1():

    # should lead to the following scenario upon insertion in
    # order:
    #
    # a->b->c
    #     ->d->e
    #        ->f
    # g
    # h->i
    #  ->j->k

    a=randomBlock()
    b=randomBlock(a)
    c=randomBlock(b)
    d=randomBlock(b)
    e=randomBlock(d)
    f=randomBlock(d)
    g=randomBlock()
    h=randomBlock()
    i=randomBlock(h)
    j=randomBlock(h)
    k=randomBlock(j)
    return [a,b,c,d,e,f,g,h,i,j,k]

s1 = scenario1()

names={}

def setName(w, name):
    #print(w, name)
    global names
    names[HashForWeak(w).GetHex()] = name


def insertBlocks(blocks):
    for idx, block in enumerate(blocks):
        util.LogPrint("weakblocks", str((numKnownWeakblocks(), numKnownWeakblockTransactions(), weakChainTips().size(), idx))+"\n")
        assert idx == numKnownWeakblocks()

        assert storeWeakblock(block)

        # storing repeatedly fails
        assert not storeWeakblock(block)

        wb = getWeakblock(block.GetHash())

        util.LogPrint("weakblocks", str((block, wb, weakHeight(wb)))+"\n")
        weakblocksConsistencyCheck()
        util.LogPrint("weakblocks", str(
            ("Current longest chain:", HashForWeak(getWeakLongestChainTip()).GetHex()))+"\n")


def insertScenario1(shuffle=False):
    a,b,c,d,e,f,g,h,i,j,k = s1

    sinsert = s1.copy()
    if shuffle:
        random.shuffle(sinsert)
    insertBlocks(sinsert)

    for w, letter in zip([getWeakblock(block.GetHash()) for block in s1], "abcdefghijk"):
        setName(w, letter)

    wa,wb,wc,wd,we,wf,wg,wh,wi,wj,wk = [getWeakblock(block.GetHash()) for block in s1]

    assert miniextendsWeak(wa) is None
    assert miniextendsWeak(wb) == wa
    assert miniextendsWeak(wc) == wb
    assert miniextendsWeak(wd) == wb
    assert miniextendsWeak(we) == wd
    assert miniextendsWeak(wf) == wd

    assert miniextendsWeak(wg) is None

    assert miniextendsWeak(wh) is None
    assert miniextendsWeak(wi) == wh
    assert miniextendsWeak(wj) == wh
    assert miniextendsWeak(wk) == wj

    if not shuffle:
        assert getWeakLongestChainTip() == we

    return s1

def printDAG(blocks):
    for b1 in blocks:
        for b2 in blocks:
            wb1 = getWeakblock(b1.GetHash())
            wb2 = getWeakblock(b2.GetHash())
            if weakExtends(wb1, wb2):
                util.LogPrint("weakblocks", names[HashForWeak(wb2).GetHex()]+" > "+names[HashForWeak(wb1).GetHex()]+"\n")

    for block in blocks:
        wb = getWeakblock(block.GetHash())
        owb = miniextendsWeak(wb)
        if owb is not None:
            util.LogPrint("weakblocks", names[HashForWeak(wb).GetHex()]+" :> "+names[HashForWeak(owb).GetHex()]+"\n")
        else:
            util.LogPrint("weakblocks", names[HashForWeak(wb).GetHex()]+" :> root\n")

for i in range(10): # repeat to test ref counting and internal structure stuff
    blocks = insertScenario1()
    printDAG(blocks)
    purgeOldWeakblocks(0)
    weakblocksConsistencyCheck()
    weakblocksEmptyCheck()

util.LogPrint("weakblocks", "RANDOMIZED\n")
for i in range(50):
    blocks = insertScenario1(shuffle=True)
    printDAG(blocks)
    purgeOldWeakblocks(0)
    weakblocksConsistencyCheck()
    weakblocksEmptyCheck()

def testRandomDAG():
    """ Create a random block graph, then insert it in a random (and usually different) order
    and check that the original DAG results. """

    # number of blocks
    N = 20

    # block with key mini-extends block with value
    miniextends={}

    blocks=[]

    # building logic for the DAG is biased in all kinds of ways, but that shouldn't matter too much
    for n in range(N):
        underlying_idx = random.randint(-1, len(blocks)-1)

        underlying = None if underlying_idx < 0 else blocks[underlying_idx]

        if underlying is not None:
            miniextends[n] = underlying_idx
        block=randomBlock(underlying)
        blocks.append(block)

    to_insert=blocks.copy()
    random.shuffle(to_insert)

    insertBlocks(to_insert)

    for n in range(N):
        wb1 = getWeakblock(blocks[n].GetHash())
        wb2 = miniextendsWeak(wb1)

        if wb2 is None: # root
            assert n not in miniextends
        else:
            assert primitives.Equals(HashForWeak(wb2), blocks[miniextends[n]].GetHash())
    purgeOldWeakblocks(0)
    weakblocksConsistencyCheck()
    weakblocksEmptyCheck()

for i in range(1000):
    print("@", i)
    testRandomDAG()
