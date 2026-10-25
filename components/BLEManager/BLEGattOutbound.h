/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGattOutbound - Outbound GATT
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <atomic>
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
    void notifyTxComplete(int statusBLEHSCode, bool isIndication);

    // Notify connection state change (used to time the post-connection publish burst)
    void notifyConnStateChanged(bool isConnected);

    // Message sending
    bool isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
    bool sendMsg(CommsChannelMsg& msg);

    // Get preferred MTU size
    uint32_t getPreferredMTUSize()
    {
        return _preferredMtuSize;
    }

    // Runtime config for indicate/notify per queue
    void setCommandUseIndication(bool useIndication) { _commandUseIndication = useIndication; }
    bool getCommandUseIndication() const { return _commandUseIndication; }
    void setPublishUseIndication(bool useIndication) { _publishUseIndication = useIndication; }
    bool getPublishUseIndication() const { return _publishUseIndication; }

private:
    // GATT server
    BLEGattServer& _gattServer;

    // Stats
    BLEManStats& _bleStats;

    // Command queue: typically uses indication (ACK'd, reliable)
    ThreadSafeQueue<ProtocolRawMsg> _commandQueue;
    std::atomic<bool> _commandUseIndication{true};
    uint32_t _commandMsgPos = 0;

    // Publish queue: typically uses notification (faster, no ACK wait)
    ThreadSafeQueue<ProtocolRawMsg> _publishQueue;
    std::atomic<bool> _publishUseIndication{false};
    uint32_t _publishMsgPos = 0;

    // Bytes in the publish queue, and the most there may be before more publishes are refused (one message may
    // take it over the limit, so a message larger than the limit is still sent when the queue is empty). A limit
    // on the count alone lets large messages (e.g. camera frames) pile up for many seconds of link time
    std::atomic<uint32_t> _publishQueueBytes{0};
    uint32_t _publishQueueMaxBytes = BLEConfig::DEFAULT_PUBLISH_QUEUE_MAX_BYTES;

    // The part of a message being sent, copied out of the queue (so the queue isn't locked during the send)
    std::vector<uint8_t> _sendChunk;

    // Time the current connection was established (0 if not connected) - used to distinguish
    // publish-queue drops during the post-connection burst from drops during normal operation
    std::atomic<uint32_t> _connStartMs{0};
    static const uint32_t PUBLISH_DROP_CONNECT_GRACE_MS = 5000;

    // Publish queue full: whether it was full when last asked, publishes refused since the last warning, and the
    // time of that warning (warnings are limited as a publisher asks again each loop while the queue is full)
    bool _publishQueueFull = false;
    uint32_t _publishRefusedSinceWarn = 0;
    uint32_t _publishFullLastWarnMs = 0;
    static const uint32_t PUBLISH_FULL_WARN_INTERVAL_MS = 1000;

    // Max time to wait for the mutex on the outbound queues
    static const uint32_t OUTBOUND_QUEUE_MAX_MS_TO_WAIT = 10;

    // Min time between adjacent outbound notification sends
    uint32_t _lastNotifySendMs = 0;
    uint32_t _minMsBetweenNotifySends = BLEConfig::BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS;

    // Task that runs the outbound queue (if enabled)
    // The task sets the handle to nullptr immediately before it exits (which is used to detect that it has exited)
    volatile TaskHandle_t _outboundMsgTaskHandle = nullptr;

    // Request for the outbound task to stop (cooperatively)
    std::atomic<bool> _outboundMsgTaskStopRequested{false};
    static const uint32_t OUTBOUND_TASK_STOP_MAX_WAIT_MS = 500;

    // Semaphore to wake outbound task when messages are enqueued or indication ACKs arrive
    SemaphoreHandle_t _outboundSemaphore = nullptr;

    // Outbound indications in flight (only applies to whichever queue uses indication)
    // This is incremented on the task which sends and decremented on the NimBLE host task
    std::atomic<int32_t> _outboundMsgsInFlight{0};
    std::atomic<uint32_t> _outbountMsgInFlightLastMs{0};
    uint32_t _outMsgsInFlightTimeoutMs = BLEConfig::BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS;

    // Max packet len
    uint16_t _maxPacketLen = BLEConfig::MAX_BLE_PACKET_LEN_DEFAULT;

    // MTU size
    uint16_t _preferredMtuSize = BLEConfig::PREFERRED_MTU_SIZE;

    // Reduce send packet size from MTU by this amount
    static const uint32_t MTU_SIZE_REDUCTION = 12;

    // Outbound queue handling
    void serviceOutboundQueue();
    bool handleSendFromQueue(ThreadSafeQueue<ProtocolRawMsg>& queue, uint32_t& msgPos, 
                bool useIndication, const char* queueName);
    void outboundMsgTask();

    // Helper to get max send length
    uint32_t getMaxSendLen() const;

    // Check if indication is in flight (with optional timeout handling)
    bool isIndicationInFlight();

    // Decrement the count of indications in flight (never goes below 0)
    int32_t decrementMsgsInFlight();
    
#endif // CONFIG_BT_ENABLED

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "BLEGattOut";
};

