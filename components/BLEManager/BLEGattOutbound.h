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
    void notifyTxComplete()
    {
        // No message in flight
        _outboundMsgInFlight = false;
    }

    // Message sending
    bool isReadyToSend(uint32_t channelID, bool& noConn);
    bool sendMsg(CommsChannelMsg& msg);

private:
    // GATT server
    BLEGattServer& _gattServer;

    // Stats
    BLEManStats& _bleStats;

    // Outbound queue for fragments of messages
    ThreadSafeQueue<ProtocolRawMsg> _bleFragmentQueue;

    // Min time between adjacent outbound messages
    uint32_t _lastOutboundMsgMs = 0;

    // Task that runs the outbound queue (if enabled)
    volatile TaskHandle_t _outboundMsgTaskHandle = nullptr;

    // Outbound message in flight
    volatile bool _outboundMsgInFlight = false;
    uint32_t _outbountMsgInFlightStartMs = 0;
    static const uint32_t BLE_OUTBOUND_MSG_IN_FLIGHT_TIMEOUT_MS = 1000;

    // Max packet len
    uint32_t _maxPacketLen = MAX_BLE_PACKET_LEN_DEFAULT;

    // Outbound queue
    void serviceOutboundQueue();
    void handleSendFromOutboundQueue();
    void outboundMsgTask();

};

