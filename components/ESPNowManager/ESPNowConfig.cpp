/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESPNowConfig
// Configuration for ESPNow transport
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ESPNowConfig.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool ESPNowPeerConfig::setup(const RaftJsonIF& config, uint32_t peerIdx, uint32_t defaultChannel, uint32_t defaultMaxPayload)
{
    name = config.getString("name", (String("peer") + String(peerIdx)).c_str());
    macStr = config.getString("mac", "");
    channel = config.getLong("channel", defaultChannel);
    encrypt = config.getBool("encrypt", false);
    lmk = config.getString("lmk", "");
    maxPayload = ESPNowConfig::constrainPayloadLen(config.getLong("maxPayload", defaultMaxPayload));
    isValid = ESPNowConfig::parseMACAddress(macStr, mac, MAC_ADDR_LEN);
    return isValid;
}

bool ESPNowPeerConfig::macMatches(const uint8_t* pMac) const
{
    return pMac && memcmp(mac, pMac, MAC_ADDR_LEN) == 0;
}

bool ESPNowConfig::setup(const RaftJsonIF& config)
{
    enable = config.getBool("enable", false);
    protocol = config.getString("protocol", "RICSerial");
    interfaceName = config.getString("interface", "ESPNow");
    wifiOwner = config.getString("wifiOwner", "netman");
    wifiMode = config.getString("wifiMode", "sta");
    channel = config.getLong("channel", 0);
    allowBroadcast = config.getBool("allowBroadcast", false);
    learnPeers = config.getBool("learnPeers", false);
    maxPeers = config.getLong("maxPeers", ESPNOW_DEFAULT_MAX_PEERS);
    maxPayload = constrainPayloadLen(config.getLong("maxPayload", ESPNOW_DEFAULT_MAX_PAYLOAD));
    txQueueMax = config.getLong("txQueueMax", config.getLong("outQSize", ESPNOW_DEFAULT_TX_QUEUE_MAX));
    pubQueueMax = config.getLong("pubQueueMax", config.getLong("pubQSize", ESPNOW_DEFAULT_PUB_QUEUE_MAX));
    rxQueueMax = config.getLong("rxQueueMax", ESPNOW_DEFAULT_RX_QUEUE_MAX);
    minMsBetweenSends = config.getLong("minMsBetweenSends", 0);
    taskEnable = config.getBool("taskEnable", false);
    taskCore = config.getLong("taskCore", 0);
    taskPriority = config.getLong("taskPriority", 1);
    taskStack = config.getLong("taskStack", 4096);
    pmk = config.getString("pmk", "");

    peers.clear();
    std::vector<String> peerConfigs;
    if (config.getArrayElems("peers", peerConfigs))
    {
        for (uint32_t peerIdx = 0; peerIdx < peerConfigs.size(); peerIdx++)
        {
            RaftJson peerJson(peerConfigs[peerIdx]);
            ESPNowPeerConfig peerConfig;
            if (peerConfig.setup(peerJson, peerIdx, channel, maxPayload))
                peers.push_back(peerConfig);
        }
    }
    return true;
}

bool ESPNowConfig::parseMACAddress(const String& macStr, uint8_t* pMacOut, uint32_t macOutLen)
{
    if (!pMacOut || (macOutLen < ESPNOW_MAC_ADDR_LEN))
        return false;

    String compactMac;
    compactMac.reserve(ESPNOW_MAC_ADDR_LEN * 2);
    for (uint32_t charIdx = 0; charIdx < macStr.length(); charIdx++)
    {
        char ch = macStr[charIdx];
        if (isxdigit((unsigned char)ch))
        {
            compactMac += ch;
        }
        else if ((ch == ':') || (ch == '-') || (ch == ' ') || (ch == '.'))
        {
            continue;
        }
        else
        {
            return false;
        }
    }
    if (compactMac.length() != ESPNOW_MAC_ADDR_LEN * 2)
        return false;

    for (uint32_t byteIdx = 0; byteIdx < ESPNOW_MAC_ADDR_LEN; byteIdx++)
    {
        char byteStr[3] = { compactMac[byteIdx * 2], compactMac[byteIdx * 2 + 1], 0 };
        char* pEnd = nullptr;
        unsigned long byteVal = strtoul(byteStr, &pEnd, 16);
        if (!pEnd || (*pEnd != 0) || (byteVal > 0xff))
            return false;
        pMacOut[byteIdx] = (uint8_t)byteVal;
    }
    return true;
}

String ESPNowConfig::formatMACAddress(const uint8_t* pMac, const char* separator)
{
    if (!pMac)
        return "";
    const char* sep = separator ? separator : "";
    String macStr;
    for (uint32_t byteIdx = 0; byteIdx < ESPNOW_MAC_ADDR_LEN; byteIdx++)
    {
        char byteStr[4];
        snprintf(byteStr, sizeof(byteStr), "%02x", pMac[byteIdx]);
        if (byteIdx != 0)
            macStr += sep;
        macStr += byteStr;
    }
    return macStr;
}

uint32_t ESPNowConfig::constrainPayloadLen(uint32_t payloadLen)
{
    if (payloadLen < ESPNOW_MIN_PAYLOAD)
        return ESPNOW_DEFAULT_MAX_PAYLOAD;
    return payloadLen;
}
