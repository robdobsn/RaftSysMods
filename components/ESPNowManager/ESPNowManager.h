/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESPNowManager
// ESPNow transport SysMod for Raft ProtocolExchange traffic
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "RaftSysMod.h"
#include "CommsChannelMsg.h"
#include "ThreadSafeQueue.h"
#include "ESPNowConfig.h"

#if defined(ESP_PLATFORM) && defined(RAFT_SYSMODS_ENABLE_ESPNOW)
#include "esp_now.h"
#endif

class APISourceInfo;

class ESPNowManager : public RaftSysMod
{
public:
    ESPNowManager(const char* pModuleName, RaftJsonIF& sysConfig);
    virtual ~ESPNowManager();

    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new ESPNowManager(pModuleName, sysConfig);
    }

protected:
    virtual void setup() override final;
    virtual void loop() override final;
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;
    virtual void addCommsChannels(CommsCoreIF& commsCore) override final;
    virtual String getStatusJSON() const override final;
    virtual String getDebugJSON() const override final;

private:
    class RxPacket
    {
    public:
        uint8_t mac[ESPNowConfig::ESPNOW_MAC_ADDR_LEN] = {0};
        std::vector<uint8_t> data;
        int8_t rssi = 0;
    };

    ESPNowConfig _config;
    ThreadSafeQueue<RxPacket> _rxQueue;
    bool _isEnabled = false;
    bool _isInit = false;
    uint16_t _nextTxSequence = 1;
    uint32_t _rxPackets = 0;
    uint32_t _rxDropped = 0;
    uint32_t _txPackets = 0;
    uint32_t _txFailed = 0;

    static ESPNowManager* _pThis;

    int findPeerIdxByMAC(const uint8_t* pMac) const;
    int findPeerIdxByChannelID(uint32_t channelID) const;
    void processRxPacket(const RxPacket& rxPacket);
    bool sendMsg(CommsChannelMsg& msg);
    bool isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
    String getPeersJSON() const;
    RaftRetCode apiStatus(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiPeers(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo);

#if defined(ESP_PLATFORM) && defined(RAFT_SYSMODS_ENABLE_ESPNOW)
    bool initESPNow();
    bool addConfiguredPeers();
    bool addPeer(const ESPNowPeerConfig& peerConfig);
    static void onDataRecvStatic(const esp_now_recv_info_t* pRecvInfo, const uint8_t* pData, int dataLen);
    static void onDataSentStatic(const esp_now_send_info_t* pTxInfo, esp_now_send_status_t status);
    void onDataRecv(const uint8_t* pMacAddr, const uint8_t* pData, int dataLen, int8_t rssi);
    void onDataSent(const uint8_t* pMacAddr, esp_now_send_status_t status);
#endif

    static constexpr const char* MODULE_PREFIX = "ESPNow";
};
