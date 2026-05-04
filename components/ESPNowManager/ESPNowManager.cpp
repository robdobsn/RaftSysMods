/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESPNowManager
// ESPNow transport SysMod for Raft ProtocolExchange traffic
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "sdkconfig.h"
#include "ESPNowManager.h"
#include "ESPNowPacketHeader.h"
#include "Logger.h"
#include "RestAPIEndpointManager.h"
#include "CommsCoreIF.h"
#include "CommsChannelSettings.h"
#include "RaftUtils.h"

#if defined(ESP_PLATFORM) && defined(RAFT_SYSMODS_ENABLE_ESPNOW)
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_err.h"
#endif

// Debug
// #define DEBUG_ESPNOW_RX
// #define DEBUG_ESPNOW_TX

ESPNowManager* ESPNowManager::_pThis = nullptr;

ESPNowManager::ESPNowManager(const char* pModuleName, RaftJsonIF& sysConfig) :
        RaftSysMod(pModuleName, sysConfig),
        _rxQueue(ESPNowConfig::ESPNOW_DEFAULT_RX_QUEUE_MAX)
{
    _pThis = this;
}

ESPNowManager::~ESPNowManager()
{
    if (_pThis == this)
        _pThis = nullptr;
}

void ESPNowManager::setup()
{
    _config.setup(modConfig());
    _rxQueue.setMaxLen(_config.rxQueueMax);
    _isEnabled = _config.enable;
    if (!_isEnabled)
        return;

#if defined(ESP_PLATFORM) && defined(RAFT_SYSMODS_ENABLE_ESPNOW)
    _isInit = initESPNow();
#else
    LOG_W(MODULE_PREFIX, "setup ESPNow selected but ESP_PLATFORM/RAFT_SYSMODS_ENABLE_ESPNOW not available");
#endif
}

void ESPNowManager::loop()
{
    if (!_isEnabled)
        return;

    static const uint32_t MAX_RX_PACKETS_PER_LOOP = 4;
    for (uint32_t packetIdx = 0; packetIdx < MAX_RX_PACKETS_PER_LOOP; packetIdx++)
    {
        RxPacket rxPacket;
        if (!_rxQueue.get(rxPacket))
            break;
        processRxPacket(rxPacket);
    }
}

void ESPNowManager::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    endpointManager.addEndpoint("espnow/status", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&ESPNowManager::apiStatus, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Get ESPNow status");
    endpointManager.addEndpoint("espnow/peers", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&ESPNowManager::apiPeers, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Get ESPNow peers");
}

void ESPNowManager::addCommsChannels(CommsCoreIF& commsCore)
{
    if (!_isEnabled)
        return;

    const CommsChannelSettings commsChannelSettings(_config.maxPayload, _config.maxPayload,
                0, 0, _config.maxPayload, _config.txQueueMax);

    for (ESPNowPeerConfig& peerConfig : _config.peers)
    {
        peerConfig.channelID = commsCore.registerChannel(
                _config.protocol.c_str(),
                _config.interfaceName.c_str(),
                peerConfig.name.c_str(),
                std::bind(&ESPNowManager::sendMsg, this, std::placeholders::_1),
                std::bind(&ESPNowManager::isReadyToSend, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                &commsChannelSettings);
    }
}

String ESPNowManager::getStatusJSON() const
{
    String statusStr = R"({"rslt":"ok")";
    statusStr += R"(,"en":)" + String(_isEnabled ? 1 : 0);
    statusStr += R"(,"init":)" + String(_isInit ? 1 : 0);
    statusStr += R"(,"peers":)" + String(_config.peers.size());
    statusStr += R"(,"rx":)" + String(_rxPackets);
    statusStr += R"(,"rxDrop":)" + String(_rxDropped);
    statusStr += R"(,"tx":)" + String(_txPackets);
    statusStr += R"(,"txFail":)" + String(_txFailed);
    statusStr += R"(,"rxQ":)" + String(const_cast<ThreadSafeQueue<RxPacket>&>(_rxQueue).count());
    statusStr += "}";
    return statusStr;
}

String ESPNowManager::getDebugJSON() const
{
    String debugStr = getStatusJSON();
    debugStr.remove(debugStr.length() - 1);
    debugStr += R"(,"peerInfo":)" + getPeersJSON() + "}";
    return debugStr;
}

int ESPNowManager::findPeerIdxByMAC(const uint8_t* pMac) const
{
    for (uint32_t peerIdx = 0; peerIdx < _config.peers.size(); peerIdx++)
    {
        if (_config.peers[peerIdx].macMatches(pMac))
            return peerIdx;
    }
    return -1;
}

int ESPNowManager::findPeerIdxByChannelID(uint32_t channelID) const
{
    for (uint32_t peerIdx = 0; peerIdx < _config.peers.size(); peerIdx++)
    {
        if (_config.peers[peerIdx].channelID == channelID)
            return peerIdx;
    }
    return -1;
}

void ESPNowManager::processRxPacket(const RxPacket& rxPacket)
{
    int peerIdx = findPeerIdxByMAC(rxPacket.mac);
    if (peerIdx < 0)
    {
        _rxDropped++;
        return;
    }
    const ESPNowPeerConfig& peerConfig = _config.peers[peerIdx];
    if ((peerConfig.channelID == UINT32_MAX) || !getCommsCore())
    {
        _rxDropped++;
        return;
    }

    uint8_t flags = 0;
    uint16_t sequenceNum = 0;
    uint8_t fragmentIdx = 0;
    uint8_t fragmentCount = 0;
    uint32_t payloadStartPos = 0;
    if (!ESPNowPacketHeader::decode(rxPacket.data.data(), rxPacket.data.size(), flags, sequenceNum,
                fragmentIdx, fragmentCount, payloadStartPos))
    {
        _rxDropped++;
        return;
    }
    if (rxPacket.data.size() <= payloadStartPos)
    {
        _rxDropped++;
        return;
    }

#ifdef DEBUG_ESPNOW_RX
    LOG_I(MODULE_PREFIX, "rx peer %s seq %d frag %d/%d flags 0x%02x len %d",
                peerConfig.name.c_str(), sequenceNum, fragmentIdx, fragmentCount, flags,
                (int)(rxPacket.data.size() - payloadStartPos));
#endif

    getCommsCore()->inboundHandleMsg(peerConfig.channelID, rxPacket.data.data() + payloadStartPos,
                rxPacket.data.size() - payloadStartPos);
    _rxPackets++;
}

bool ESPNowManager::sendMsg(CommsChannelMsg& msg)
{
    int peerIdx = findPeerIdxByChannelID(msg.getChannelID());
    if (peerIdx < 0)
    {
        _txFailed++;
        return false;
    }
    ESPNowPeerConfig& peerConfig = _config.peers[peerIdx];
    uint32_t maxPayload = peerConfig.maxPayload > 0 ? peerConfig.maxPayload : _config.maxPayload;
    maxPayload = ESPNowConfig::constrainPayloadLen(maxPayload);
    if (maxPayload <= ESPNowPacketHeader::HEADER_LEN)
    {
        _txFailed++;
        return false;
    }
    uint32_t maxDataPerPacket = maxPayload - ESPNowPacketHeader::HEADER_LEN;
    uint32_t fragmentCount = (msg.getBufLen() + maxDataPerPacket - 1) / maxDataPerPacket;
    if ((fragmentCount == 0) || (fragmentCount > 255))
    {
        _txFailed++;
        return false;
    }

    uint16_t sequenceNum = _nextTxSequence++;
    for (uint32_t fragmentIdx = 0; fragmentIdx < fragmentCount; fragmentIdx++)
    {
        uint32_t dataPos = fragmentIdx * maxDataPerPacket;
        uint32_t dataLen = msg.getBufLen() - dataPos;
        if (dataLen > maxDataPerPacket)
            dataLen = maxDataPerPacket;

        std::vector<uint8_t> packet;
        packet.resize(ESPNowPacketHeader::HEADER_LEN + dataLen);
        uint8_t flags = 0;
        if (fragmentIdx == 0)
            flags |= ESPNowPacketHeader::FLAG_START;
        if (fragmentIdx == fragmentCount - 1)
            flags |= ESPNowPacketHeader::FLAG_END;
        ESPNowPacketHeader::encode(packet.data(), packet.size(), flags, sequenceNum,
                    fragmentIdx, fragmentCount);
        memcpy(packet.data() + ESPNowPacketHeader::HEADER_LEN, msg.getBuf() + dataPos, dataLen);

#if defined(ESP_PLATFORM) && defined(RAFT_SYSMODS_ENABLE_ESPNOW)
        esp_err_t err = esp_now_send(peerConfig.mac, packet.data(), packet.size());
        if (err != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "sendMsg peer %s failed err %s (%d)",
                        peerConfig.name.c_str(), esp_err_to_name(err), err);
            _txFailed++;
            return false;
        }
#else
        _txFailed++;
        return false;
#endif
        _txPackets++;
    }
    return true;
}

bool ESPNowManager::isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
{
    (void)msgType;
    noConn = !_isInit || (findPeerIdxByChannelID(channelID) < 0);
    return !noConn;
}

String ESPNowManager::getPeersJSON() const
{
    String peersStr = "[";
    for (uint32_t peerIdx = 0; peerIdx < _config.peers.size(); peerIdx++)
    {
        const ESPNowPeerConfig& peerConfig = _config.peers[peerIdx];
        if (peerIdx != 0)
            peersStr += ",";
        peersStr += R"({"name":")" + peerConfig.name + R"(")";
        peersStr += R"(,"mac":")" + ESPNowConfig::formatMACAddress(peerConfig.mac) + R"(")";
        peersStr += R"(,"chanID":)" + String(peerConfig.channelID);
        peersStr += R"(,"channel":)" + String(peerConfig.channel);
        peersStr += R"(,"encrypt":)" + String(peerConfig.encrypt ? 1 : 0);
        peersStr += "}";
    }
    peersStr += "]";
    return peersStr;
}

RaftRetCode ESPNowManager::apiStatus(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    (void)reqStr;
    (void)sourceInfo;
    respStr = getStatusJSON();
    return RAFT_OK;
}

RaftRetCode ESPNowManager::apiPeers(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    (void)reqStr;
    (void)sourceInfo;
    respStr = R"({"rslt":"ok","peers":)" + getPeersJSON() + "}";
    return RAFT_OK;
}

#if defined(ESP_PLATFORM) && defined(RAFT_SYSMODS_ENABLE_ESPNOW)

bool ESPNowManager::initESPNow()
{
    esp_err_t err = esp_now_init();
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "initESPNow esp_now_init failed err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    if (!_config.pmk.isEmpty())
        esp_now_set_pmk(reinterpret_cast<const uint8_t*>(_config.pmk.c_str()));

    esp_now_register_recv_cb(&ESPNowManager::onDataRecvStatic);
    esp_now_register_send_cb(&ESPNowManager::onDataSentStatic);

    bool peersOk = addConfiguredPeers();
    LOG_I(MODULE_PREFIX, "setup %s peers %d maxPayload %d learnPeers %s",
                peersOk ? "OK" : "PEERFAIL", (int)_config.peers.size(), (int)_config.maxPayload,
                _config.learnPeers ? "Y" : "N");
    return peersOk;
}

bool ESPNowManager::addConfiguredPeers()
{
    bool allOk = true;
    for (const ESPNowPeerConfig& peerConfig : _config.peers)
    {
        if (!addPeer(peerConfig))
            allOk = false;
    }
    return allOk;
}

bool ESPNowManager::addPeer(const ESPNowPeerConfig& peerConfig)
{
    if (!peerConfig.isValid)
        return false;

    if (esp_now_is_peer_exist(peerConfig.mac))
        return true;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerConfig.mac, ESPNowConfig::ESPNOW_MAC_ADDR_LEN);
    peerInfo.channel = peerConfig.channel;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = peerConfig.encrypt;
    if (peerConfig.encrypt && !peerConfig.lmk.isEmpty())
        memcpy(peerInfo.lmk, peerConfig.lmk.c_str(), peerConfig.lmk.length() > ESP_NOW_KEY_LEN ? ESP_NOW_KEY_LEN : peerConfig.lmk.length());

    esp_err_t err = esp_now_add_peer(&peerInfo);
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "addPeer %s mac %s failed err %s (%d)",
                    peerConfig.name.c_str(), ESPNowConfig::formatMACAddress(peerConfig.mac).c_str(),
                    esp_err_to_name(err), err);
        return false;
    }
    return true;
}

void ESPNowManager::onDataRecvStatic(const esp_now_recv_info_t* pRecvInfo, const uint8_t* pData, int dataLen)
{
    if (!_pThis || !pRecvInfo)
        return;
    int8_t rssi = 0;
    if (pRecvInfo->rx_ctrl)
        rssi = pRecvInfo->rx_ctrl->rssi;
    _pThis->onDataRecv(pRecvInfo->src_addr, pData, dataLen, rssi);
}

void ESPNowManager::onDataSentStatic(const esp_now_send_info_t* pTxInfo, esp_now_send_status_t status)
{
    if (_pThis)
    _pThis->onDataSent(pTxInfo ? pTxInfo->des_addr : nullptr, status);
}

void ESPNowManager::onDataRecv(const uint8_t* pMacAddr, const uint8_t* pData, int dataLen, int8_t rssi)
{
    if (!pMacAddr || !pData || (dataLen <= 0))
    {
        _rxDropped++;
        return;
    }

    RxPacket rxPacket;
    memcpy(rxPacket.mac, pMacAddr, ESPNowConfig::ESPNOW_MAC_ADDR_LEN);
    rxPacket.data.assign(pData, pData + dataLen);
    rxPacket.rssi = rssi;
    if (!_rxQueue.put(rxPacket))
        _rxDropped++;
}

void ESPNowManager::onDataSent(const uint8_t* pMacAddr, esp_now_send_status_t status)
{
    (void)pMacAddr;
    if (status != ESP_NOW_SEND_SUCCESS)
        _txFailed++;
}

#endif
