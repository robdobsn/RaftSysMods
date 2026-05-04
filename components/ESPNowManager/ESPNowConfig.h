/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESPNowConfig
// Configuration for ESPNow transport
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftJsonIF.h"
#include "RaftJson.h"

class ESPNowPeerConfig
{
public:
    static const uint32_t MAC_ADDR_LEN = 6;

    bool setup(const RaftJsonIF& config, uint32_t peerIdx, uint32_t defaultChannel, uint32_t defaultMaxPayload);
    bool macMatches(const uint8_t* pMac) const;

    String name;
    String macStr;
    uint8_t mac[MAC_ADDR_LEN] = {0};
    uint32_t channel = 0;
    bool encrypt = false;
    String lmk;
    uint32_t maxPayload = 0;
    uint32_t channelID = UINT32_MAX;
    bool isValid = false;
};

class ESPNowConfig
{
public:
    static const uint32_t ESPNOW_MAC_ADDR_LEN = 6;
    static const uint32_t ESPNOW_DEFAULT_MAX_PAYLOAD = 250;
    static const uint32_t ESPNOW_HEADER_LEN = 8;
    static const uint32_t ESPNOW_MIN_PAYLOAD = ESPNOW_HEADER_LEN + 1;
    static const uint32_t ESPNOW_DEFAULT_MAX_PEERS = 4;
    static const uint32_t ESPNOW_DEFAULT_TX_QUEUE_MAX = 12;
    static const uint32_t ESPNOW_DEFAULT_PUB_QUEUE_MAX = 8;
    static const uint32_t ESPNOW_DEFAULT_RX_QUEUE_MAX = 20;

    bool setup(const RaftJsonIF& config);

    static bool parseMACAddress(const String& macStr, uint8_t* pMacOut, uint32_t macOutLen = ESPNOW_MAC_ADDR_LEN);
    static String formatMACAddress(const uint8_t* pMac, const char* separator = ":");
    static uint32_t constrainPayloadLen(uint32_t payloadLen);

    bool enable = false;
    String protocol = "RICSerial";
    String interfaceName = "ESPNow";
    String wifiOwner = "netman";
    String wifiMode = "sta";
    uint32_t channel = 0;
    bool allowBroadcast = false;
    bool learnPeers = false;
    uint32_t maxPeers = ESPNOW_DEFAULT_MAX_PEERS;
    uint32_t maxPayload = ESPNOW_DEFAULT_MAX_PAYLOAD;
    uint32_t txQueueMax = ESPNOW_DEFAULT_TX_QUEUE_MAX;
    uint32_t pubQueueMax = ESPNOW_DEFAULT_PUB_QUEUE_MAX;
    uint32_t rxQueueMax = ESPNOW_DEFAULT_RX_QUEUE_MAX;
    uint32_t minMsBetweenSends = 0;
    bool taskEnable = false;
    int32_t taskCore = 0;
    int32_t taskPriority = 1;
    uint32_t taskStack = 4096;
    String pmk;
    std::vector<ESPNowPeerConfig> peers;
};
