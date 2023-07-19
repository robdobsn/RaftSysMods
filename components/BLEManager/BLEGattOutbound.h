/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGattOutbound - Outbound GATT
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <ThreadSafeQueue.h>
#include <ProtocolRawMsg.h>
#include <CommsChannelMsg.h>

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
    static const uint32_t BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS = 25;
    static const uint32_t MAX_BLE_PACKET_LEN_DEFAULT = 450;

    // Constructor
    BLEGattOutbound(BLEGattServer& gattServer, BLEManStats& bleStats);
    virtual ~BLEGattOutbound();

    // Setup
    bool setup(uint32_t maxPacketLen, uint32_t outboundQueueSize, bool useTaskForSending,
                UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize);

    // Service
    void service();

    // Stop
    void stop();

    // Tx complete
    void notifyTxComplete(int statusBLEHSCode);

    // Message sending
    bool isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
    bool sendMsg(CommsChannelMsg& msg);

private:
    // GATT server
    BLEGattServer& _gattServer;

    // Stats
    BLEManStats& _bleStats;

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
    uint32_t _outboundMsgsInFlightMax = 6;
    uint32_t _outbountMsgInFlightLastMs = 0;
    static const uint32_t BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS = 1000;

    // Mutex for queue
    SemaphoreHandle_t _inFlightMutex = nullptr;
    static const uint32_t WAIT_FOR_INFLIGHT_MUTEX_MAX_MS = 2;

    // Max packet len
    uint32_t _maxPacketLen = MAX_BLE_PACKET_LEN_DEFAULT;

    // Outbound queue
    void serviceOutboundQueue();
    bool handleSendFromOutboundQueue();
    void outboundMsgTask();

};
