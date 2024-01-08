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

class CommsChannelMsg;
class BLEGattServer;
class BLEManStats;

class BLEGattOutbound
{
public:
    // Defaults
    static const bool DEFAULT_USE_TASK_FOR_SENDING = false;
    static const int DEFAULT_TASK_CORE = 0;
    static const int DEFAULT_TASK_PRIORITY = 1;
    static const int DEFAULT_TASK_SIZE_BYTES = 4000;
    static const int DEFAULT_OUTBOUND_MSG_QUEUE_SIZE = 30;
    static const uint32_t BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS = 50;
    static const uint32_t MAX_BLE_PACKET_LEN_DEFAULT = 500;
    static const uint32_t PREFERRED_MTU_VALUE = 512;
    static const uint32_t DEFAULT_NUM_OUTBOUND_MSGS_IN_FLIGHT_MAX = 10;
    static const uint32_t BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS = 500;

#include "sdkconfig.h"
#ifdef CONFIG_BT_ENABLED

    // Constructor
    BLEGattOutbound(BLEGattServer& gattServer, BLEManStats& bleStats);
    virtual ~BLEGattOutbound();

    // Setup
    bool setup(uint32_t maxPacketLen, uint32_t outboundQueueSize, bool useTaskForSending,
                UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize,
                bool sendUsingIndication);

    // Service
    void service();

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
        _mtuSize = mtuSize;
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
    uint32_t _outboundMsgPos = 0;

    // Min time between adjacent outbound messages
    uint32_t _lastOutboundMsgMs = 0;

    // Task that runs the outbound queue (if enabled)
    volatile TaskHandle_t _outboundMsgTaskHandle = nullptr;

    // Outbound messages in flight
    volatile uint32_t _outboundMsgsInFlight = 0;
    uint32_t _outboundMsgsInFlightMax = DEFAULT_NUM_OUTBOUND_MSGS_IN_FLIGHT_MAX;
    uint32_t _outbountMsgInFlightLastMs = 0;

    // Mutex for in flight variable
    SemaphoreHandle_t _inFlightMutex = nullptr;
    static const uint32_t WAIT_FOR_INFLIGHT_MUTEX_MAX_MS = 2;

    // Max packet len
    uint32_t _maxPacketLen = MAX_BLE_PACKET_LEN_DEFAULT;

    // MTU size
    uint32_t _mtuSize = PREFERRED_MTU_VALUE;

    // Reduce send packet size from MTU by this amount
    static const uint32_t MTU_SIZE_REDUCTION = 12;

    // Outbound queue
    void serviceOutboundQueue();
    bool handleSendFromOutboundQueue();
    void outboundMsgTask();
    
#endif // CONFIG_BT_ENABLED
};

