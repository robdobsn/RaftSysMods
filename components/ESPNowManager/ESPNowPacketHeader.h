/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESPNowPacketHeader
// Small transport header for Raft ESPNow packets
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

class ESPNowPacketHeader
{
public:
    static const uint8_t MAGIC = 0x52;
    static const uint8_t VERSION = 1;
    static const uint8_t HEADER_LEN = 8;

    enum Flags
    {
        FLAG_START = 0x01,
        FLAG_END = 0x02,
        FLAG_ACK_REQUESTED = 0x04,
        FLAG_ACK_FRAME = 0x08
    };

    static bool encode(uint8_t* pBuf, uint32_t bufLen, uint8_t flags, uint16_t sequenceNum,
                uint8_t fragmentIdx, uint8_t fragmentCount)
    {
        if (!pBuf || (bufLen < HEADER_LEN))
            return false;
        pBuf[0] = MAGIC;
        pBuf[1] = VERSION;
        pBuf[2] = flags;
        pBuf[3] = HEADER_LEN;
        pBuf[4] = sequenceNum & 0xff;
        pBuf[5] = (sequenceNum >> 8) & 0xff;
        pBuf[6] = fragmentIdx;
        pBuf[7] = fragmentCount;
        return true;
    }

    static bool decode(const uint8_t* pBuf, uint32_t bufLen, uint8_t& flags, uint16_t& sequenceNum,
                uint8_t& fragmentIdx, uint8_t& fragmentCount, uint32_t& payloadStartPos)
    {
        if (!pBuf || (bufLen < HEADER_LEN) || (pBuf[0] != MAGIC) || (pBuf[1] != VERSION) ||
                (pBuf[3] < HEADER_LEN) || (pBuf[3] > bufLen))
            return false;
        flags = pBuf[2];
        sequenceNum = pBuf[4] | (pBuf[5] << 8);
        fragmentIdx = pBuf[6];
        fragmentCount = pBuf[7];
        payloadStartPos = pBuf[3];
        return true;
    }
};
