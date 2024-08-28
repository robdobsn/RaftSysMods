/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGattOutbound - Outbound GATT
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "ThreadSafeQueue.h"
#include "ProtocolRawMsg.h"
#include "CommsChannelMsg.h"
#include "BLEConfig.h"

class CommsChannelMsg;
class BLEGattServer;
class BLEManStats;

class BLEGattOutbound
{
public:
#include "sdkconfig.h"
#ifdef CONFIG_BT_ENABLED

    // Constructor
    BLEGattOutbound(BLEGattServer& gattServer, BLEManStats& bleStats);
    virtual ~BLEGattOutbound();

    // Setup
    bool setup(const BLEConfig& bleConfig);

    // Service
    void loop();

    // Stop
    void stop();

    // Tx complete
    void notifyTxComplete(int statusBLEHSCode);

    // Message sending
    bool isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
    bool sendMsg(CommsChannelMsg& msg);

    // Inform of MTU size
    void onMTUSizeInfo(uint32_t mtuSize)
    {
        _actualMtuSize = mtuSize;
    }

    // Get preferred MTU size
    uint32_t getPreferredMTUSize()
    {
        return _preferredMtuSize;
    }

private:
    // GATT server
    BLEGattServer& _gattServer;

    // Stats
    BLEManStats& _bleStats;

    // Send using indication
    bool _sendUsingIndication = false;

    // Outbound queue of messages
    ThreadSafeQueue<ProtocolRawMsg> _outboundQueue;

    // Position in current message being sent
    uint16_t _outboundMsgPos = 0;

    // Min time between adjacent outbound messages
    uint32_t _lastOutboundMsgMs = 0;
    uint32_t _minMsBetweenSends = BLEConfig::BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS;

    // Task that runs the outbound queue (if enabled)
    volatile TaskHandle_t _outboundMsgTaskHandle = nullptr;

    // Outbound messages in flight
    volatile uint32_t _outboundMsgsInFlight = 0;
    uint16_t _outMsgsInFlightMax = BLEConfig::DEFAULT_NUM_OUTBOUND_MSGS_IN_FLIGHT_MAX;
    uint32_t _outbountMsgInFlightLastMs = 0;
    uint32_t _outMsgsInFlightTimeoutMs = BLEConfig::BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS;

    // Mutex for in flight variable
    SemaphoreHandle_t _inFlightMutex = nullptr;
    static const uint32_t WAIT_FOR_INFLIGHT_MUTEX_MAX_MS = 2;

    // Max packet len
    uint16_t _maxPacketLen = BLEConfig::MAX_BLE_PACKET_LEN_DEFAULT;

    // MTU size
    uint16_t _preferredMtuSize = BLEConfig::PREFERRED_MTU_SIZE;
    uint16_t _actualMtuSize = BLEConfig::PREFERRED_MTU_SIZE;

    // Reduce send packet size from MTU by this amount
    static const uint32_t MTU_SIZE_REDUCTION = 12;

    // Outbound queue
    void serviceOutboundQueue();
    bool handleSendFromOutboundQueue();
    void outboundMsgTask();
    
#endif // CONFIG_BT_ENABLED

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "BLEGattOut";
};

