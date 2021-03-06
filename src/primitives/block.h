// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include "hash.h"
#include "crypto/common.h"
#include "crypto/scrypt.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"

class CBlockIndex;
class CWallet;

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    static const int LEGACY_VERSION_2 = 2;
    static const int LEGACY_VERSION_3 = 3; // SolarCoin: Transitional version for Legacy nodes with a bug in 'getheaders'.
    static const int CURRENT_VERSION = 4;  // SolarCoin 3.15.1.0
    int nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    unsigned int nTime;
    unsigned int nBits;
    unsigned int nNonce;

    CBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const
    {
        return Hash(BEGIN(nVersion), END(nNonce));
    }

    // ppcoin: entropy bit for stake modifier if chosen by modifier
    unsigned int GetStakeEntropyBit(unsigned int nTime) const
    {
        // Take last bit of block hash as entropy bit
        unsigned int nEntropyBit = ((GetHash().GetUint64(0)) & 1llu);
        if (fDebug || GetBoolArg("-printstakemodifier", false))
            LogPrintf("GetStakeEntropyBit: nTime=%u hashBlock=%s nEntropyBit=%u\n", nTime, GetHash().ToString().c_str(), nEntropyBit);
        return nEntropyBit;
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    void UpdateTime(const CBlockIndex* pindexPrev);

    std::string ToString() const;
};


class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // ppcoin: block signature - signed by one of the coin base txout[N]'s owner
    std::vector<unsigned char> vchBlockSig;

    // memory only
    mutable std::vector<uint256> vMerkleTree;

    // TODO: This has been re-worked in bitcoin core
    // Denial-of-service detection:
    mutable int nDoS;
    bool DoS(int nDoSIn, bool fIn) const { nDoS += nDoSIn; return fIn; }

    // memory only
    mutable bool fChecked;

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        const bool fHeaderOnly = (s.GetType() & SER_BLOCKHEADERONLY);
        READWRITE(*(CBlockHeader*)this);
        // PoST: ConnectBlock depends on vtx following header to generate CDiskTxPos
        if (!fHeaderOnly || this->nVersion >= CBlockHeader::LEGACY_VERSION_3) {
            READWRITE(vtx);
            if (!fHeaderOnly && this->nVersion >= CBlockHeader::LEGACY_VERSION_3) {
                READWRITE(vchBlockSig);
            }
        } else {
            if (ser_action.ForRead()) {
                const_cast<CBlock*>(this)->vtx.clear();
                const_cast<CBlock*>(this)->vchBlockSig.clear();
            }
        }
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        vchBlockSig.clear();
        vMerkleTree.clear();
        nDoS = 0;
        fChecked = false;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        return block;
    }

    std::string ToString() const;

    /* SolarCoin methods */

    uint256 GetPoWHash() const
    {
        uint256 thash;
        scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
        return thash;
    }

    // ppcoin: two types of block: proof-of-work or proof-of-stake
    bool IsProofOfStake() const
    {
        const CTransaction& tx = *vtx[1];
        return (vtx.size() > 1 && tx.IsCoinStake());
    }

    bool IsProofOfWork() const
    {
        return !IsProofOfStake();
    }

    std::pair<COutPoint, unsigned int> GetProofOfStake() const
    {
        const CTransaction& tx = *vtx[1];
        return IsProofOfStake() ? std::make_pair(tx.vin[0].prevout, tx.nTime) : std::make_pair(COutPoint(), (unsigned int)0);
    }

    bool SignBlock(CWallet& wallet, int64_t nFees);
    bool CheckBlockSignature(bool fProofOfStake) const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    CBlockLocator(const std::vector<uint256>& vHaveIn) : vHave(vHaveIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

/** Compute the consensus-critical block weight (see BIP 141). */
int64_t GetBlockWeight(const CBlock& tx);

#endif // BITCOIN_PRIMITIVES_BLOCK_H
