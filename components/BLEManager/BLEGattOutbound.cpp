/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGattOutbound - Outbound GATT
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "sdkconfig.h"
#ifdef CONFIG_BT_ENABLED

#include "BLEGattOutbound.h"
#include "CommsChannelMsg.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "BLEGattServer.h"
#include "BLEManStats.h"

// Warn
#define WARN_ON_OUTBOUND_MSG_TIMEOUT
#define WARN_ON_PUBLISH_QUEUE_FULL

// Debug
// #define DEBUG_SEND_FROM_OUTBOUND_QUEUE
// #define DEBUG_BLE_TX_MSG
// #define DEBUG_BLE_PUBLISH

BLEGattOutbound::BLEGattOutbound(BLEGattServer& gattServer, BLEManStats& bleStats) :
        _gattServer(gattServer),
        _bleStats(bleStats),
        _commandQueue(BLEConfig::DEFAULT_OUTBOUND_MSG_QUEUE_SIZE),
        _publishQueue(BLEConfig::DEFAULT_PUBLISH_QUEUE_SIZE)
{
    _outboundSemaphore = xSemaphoreCreateBinary();
}

BLEGattOutbound::~BLEGattOutbound()
{
    if (_outboundSemaphore)
        vSemaphoreDelete(_outboundSemaphore);
}

// Setup
bool BLEGattOutbound::setup(const BLEConfig& bleConfig)
{
    // Max len
    _maxPacketLen = bleConfig.maxPacketLen;
    _preferredMtuSize = bleConfig.preferredMTUSize;
    _actualMtuSize = bleConfig.preferredMTUSize;

    // Send params - command queue uses the global sendUseInd setting, publish queue uses publishUseIndication
    _commandUseIndication = bleConfig.sendUsingIndication;
    _publishUseIndication = bleConfig.publishUseIndication;
    _outMsgsInFlightTimeoutMs = bleConfig.outMsgsInFlightTimeoutMs;
    _minMsBetweenNotifySends = bleConfig.minMsBetweenSends;

    // Setup queues
    _commandQueue.setMaxLen(bleConfig.outboundQueueSize);
    _publishQueue.setMaxLen(bleConfig.publishQueueSize);

    // Check if a thread should be started for sending
    if (bleConfig.useTaskForSending)
    {
        // Start the worker task
        BaseType_t retc = pdPASS;
        if (_outboundMsgTaskHandle == nullptr)
        {
            _outboundMsgTaskStopRequested = false;
            retc = xTaskCreatePinnedToCore(
                        [](void* pArg){
                            ((BLEGattOutbound*)pArg)->outboundMsgTask();
                        },
                        "BLEOutQ",                              // task name
                        bleConfig.taskStackSize,                          // stack size of task
                        this,                                   // parameter passed to task on execute
                        bleConfig.taskPriority,                           // priority
                        (TaskHandle_t*)&_outboundMsgTaskHandle, // task handle
                        bleConfig.taskCore);                              // pin task to core N
            if (retc != pdPASS)
            {
                LOG_W(MODULE_PREFIX, "setup outbound msg task failed");
            }
            return retc == pdPASS;
        }
    }
    return true;
}

void BLEGattOutbound::loop()
{
    // Service the outbound queue
    serviceOutboundQueue();
}

void BLEGattOutbound::stop()
{
    // Stop the worker task
    if (_outboundMsgTaskHandle)
    {
        // Request the task to exit cooperatively (so it is not deleted while holding a queue mutex or
        // in a call to the BLE stack) and wake it if it is waiting
        _outboundMsgTaskStopRequested = true;
        if (_outboundSemaphore)
            xSemaphoreGive(_outboundSemaphore);

        // Wait for the task to exit (it sets the handle to nullptr immediately before exiting)
        uint32_t waitStartMs = millis();
        while ((_outboundMsgTaskHandle != nullptr) &&
                    !Raft::isTimeout(millis(), waitStartMs, OUTBOUND_TASK_STOP_MAX_WAIT_MS))
        {
            vTaskDelay(1);
        }

        // Fall back to deleting the task if it didn't exit
        TaskHandle_t taskHandle = _outboundMsgTaskHandle;
        if (taskHandle != nullptr)
        {
            LOG_W(MODULE_PREFIX, "stop outbound msg task did not exit - deleting");
            vTaskDelete(taskHandle);
            _outboundMsgTaskHandle = nullptr;
        }
        _outboundMsgTaskStopRequested = false;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service outbound queue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattOutbound::serviceOutboundQueue()
{
    // Send if not using alternate thread
    if (_outboundMsgTaskHandle == nullptr)
    {
        // Service command queue first (higher priority), then publish queue
        // Don't interleave: if one queue has a partially-sent HDLC message,
        // don't send from the other queue (would corrupt the HDLC framing)
        if (_publishMsgPos == 0)
            handleSendFromQueue(_commandQueue, _commandMsgPos, _commandUseIndication, "cmd");
        if (_commandMsgPos == 0)
            handleSendFromQueue(_publishQueue, _publishMsgPos, _publishUseIndication, "pub");
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattOutbound::isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
{
    if (msgType == MSG_TYPE_PUBLISH)
    {
        bool canSend = _publishQueue.count() < _publishQueue.maxLen();
#ifdef WARN_ON_PUBLISH_QUEUE_FULL
        if (!canSend)
        {
            LOG_W(MODULE_PREFIX, "isReadyToSend PUBLISH DROPPED qCount %d maxLen %d",
                        _publishQueue.count(), _publishQueue.maxLen());
        }
#endif
        return canSend;
    }

    return _commandQueue.count() < _commandQueue.maxLen();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattOutbound::sendMsg(CommsChannelMsg& msg)
{
#ifdef DEBUG_BLE_TX_MSG
#ifndef DEBUG_BLE_PUBLISH
    if (msg.getMsgTypeCode() != MSG_TYPE_PUBLISH) 
    {
#endif
        LOG_I(MODULE_PREFIX, "sendBLEMsg channelID %d, msgType %s msgNum %d, len %d msg %s",
            msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen(), msg.getBuf());
#ifndef DEBUG_BLE_PUBLISH
    }
#endif
#endif

    // Route to appropriate queue based on message type
    ProtocolRawMsg bleOutMsg(msg.getBuf(), msg.getBufLen());
    bool putOk = false;
    if (msg.getMsgTypeCode() == MSG_TYPE_PUBLISH)
    {
        putOk = _publishQueue.put(bleOutMsg, OUTBOUND_QUEUE_MAX_MS_TO_WAIT);
        if (!putOk)
        {
            LOG_W(MODULE_PREFIX, "sendBLEMsg PUBLISH FAILEDTOSEND totalLen %d qCount %d", 
                        msg.getBufLen(), _publishQueue.count());
        }
    }
    else
    {
        putOk = _commandQueue.put(bleOutMsg, OUTBOUND_QUEUE_MAX_MS_TO_WAIT);
        if (!putOk)
        {
            LOG_W(MODULE_PREFIX, "sendBLEMsg CMD FAILEDTOSEND totalLen %d qCount %d", 
                        msg.getBufLen(), _commandQueue.count());
        }
    }

    // Wake the outbound task if it is waiting
    if (putOk && _outboundSemaphore)
        xSemaphoreGive(_outboundSemaphore);

    return putOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get max send length based on MTU
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t BLEGattOutbound::getMaxSendLen() const
{
    uint16_t actualMtuSize = _actualMtuSize.load();
    return ((actualMtuSize != 0) && (actualMtuSize > MTU_SIZE_REDUCTION+1))
            ? actualMtuSize - MTU_SIZE_REDUCTION : _maxPacketLen;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if an indication is in flight (with timeout handling)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattOutbound::isIndicationInFlight()
{
    if (_outboundMsgsInFlight.load() > 0)
    {
        // Check for timeout on in flight messages
        if (Raft::isTimeout(millis(), _outbountMsgInFlightLastMs.load(), _outMsgsInFlightTimeoutMs))
        {
#ifdef WARN_ON_OUTBOUND_MSG_TIMEOUT
            LOG_W(MODULE_PREFIX, "isIndicationInFlight msg timeout - clearing inFlight");
#endif
            _outboundMsgsInFlight = 0;
            return false;
        }
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decrement the count of indications in flight (never goes below 0)
// This may be called on any task
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int32_t BLEGattOutbound::decrementMsgsInFlight()
{
    int32_t msgsInFlight = _outboundMsgsInFlight.load();
    while (msgsInFlight > 0)
    {
        // Note that compare_exchange_weak updates msgsInFlight if it fails
        if (_outboundMsgsInFlight.compare_exchange_weak(msgsInFlight, msgsInFlight - 1))
            return msgsInFlight - 1;
    }
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle sending from a specific queue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattOutbound::handleSendFromQueue(ThreadSafeQueue<ProtocolRawMsg>& queue, 
                uint16_t& msgPos, bool useIndication, const char* queueName)
{
    // If this queue uses indication, check if an indication is already in flight
    if (useIndication && isIndicationInFlight())
        return false;

    // If this queue uses notification, check time since last notify send
    if (!useIndication)
    {
        if (!Raft::isTimeout(millis(), _lastNotifySendMs, _minMsBetweenNotifySends))
            return false;
    }

    // Peek next message in queue
    ProtocolRawMsg bleOutMsg;
    if (!queue.peek(bleOutMsg, OUTBOUND_QUEUE_MAX_MS_TO_WAIT))
        return false;

    // Extract next section of message to send
    uint32_t toSendLen = 0;
    bool removeFromQueue = true;
    if (bleOutMsg.getBufLen() > msgPos)
        toSendLen = bleOutMsg.getBufLen() - msgPos;
    uint32_t maxLen = getMaxSendLen();
    if (toSendLen > maxLen)
        toSendLen = maxLen;
    if (bleOutMsg.getBufLen() > msgPos + toSendLen)
        removeFromQueue = false;

    BLEGattServerSendResult rslt = BLEGATT_SERVER_SEND_RESULT_TRY_AGAIN;
    if (toSendLen != 0)
    {
        // Handle messages in flight calculation when using indication
        if (useIndication)
        {
            _outbountMsgInFlightLastMs = millis();
            _outboundMsgsInFlight++;
        }

        // Record last notify send time
        if (!useIndication)
            _lastNotifySendMs = millis();

        // Send to central
        rslt = _gattServer.sendToCentral(bleOutMsg.getBuf() + msgPos, toSendLen, useIndication);
        if (rslt == BLEGATT_SERVER_SEND_RESULT_OK)
        {
            _bleStats.txMsg(toSendLen, true);
            msgPos += toSendLen;
        }

        // Handle try-again failures
        else if (rslt == BLEGATT_SERVER_SEND_RESULT_TRY_AGAIN)
        {
            // Try again later
            removeFromQueue = false;
        }

        // Check if failed
        else
        {
            _bleStats.txMsg(0, false);
            removeFromQueue = true;
        }

        // Handle messages in flight calculation when using indication - decrement on failure
        if ((rslt != BLEGATT_SERVER_SEND_RESULT_OK) && useIndication)
        {
            decrementMsgsInFlight();
        }
    }

    // Remove from queue if required
    if (removeFromQueue)
    {
        // The message is only considered removed if the queue confirms removal (this is the only consumer
        // so the message at the front of the queue is the one peeked above)
        if (queue.pop(OUTBOUND_QUEUE_MAX_MS_TO_WAIT))
        {
            msgPos = 0;
        }
        else
        {
            // Failed to remove (queue mutex not obtained) so set the position to the end of the message to
            // ensure that no part of it is sent again - removal will be attempted again on the next call
            msgPos = bleOutMsg.getBufLen() > UINT16_MAX ? UINT16_MAX : bleOutMsg.getBufLen();
        }
    }

#ifdef DEBUG_SEND_FROM_OUTBOUND_QUEUE
    if (useIndication)
    {
        int32_t msgsInFlight = _outboundMsgsInFlight.load();
        LOG_I(MODULE_PREFIX, "handleSendFromQ %s sendLen %d totalLen %d msgPos %d sendOk %d inFlight %d leftInQ %d removeFromQ %d", 
                queueName, toSendLen, bleOutMsg.getBufLen(), msgPos, rslt, msgsInFlight, queue.count(), removeFromQueue);
    }
    else
    {
        LOG_I(MODULE_PREFIX, "handleSendFromQ %s sendLen %d totalLen %d msgPos %d sendOk %s leftInQ %d removeFromQ %d", 
            queueName, toSendLen, bleOutMsg.getBufLen(), msgPos, 
            rslt == BLEGATT_SERVER_SEND_RESULT_OK ? "OK" : rslt == BLEGATT_SERVER_SEND_RESULT_FAIL ? "FAIL" : "TRYAGAIN", 
            queue.count(), removeFromQueue);
    }
#endif

    return rslt == BLEGATT_SERVER_SEND_RESULT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Task worker for outbound messages
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattOutbound::outboundMsgTask()
{
    // Run the task until asked to stop
    while (!_outboundMsgTaskStopRequested)
    {        
        // Handle both queues
        // Don't interleave: if one queue has a partially-sent HDLC message,
        // don't send from the other queue (would corrupt the HDLC framing)
        if (_publishMsgPos == 0)
            handleSendFromQueue(_commandQueue, _commandMsgPos, _commandUseIndication, "cmd");
        if (_commandMsgPos == 0)
            handleSendFromQueue(_publishQueue, _publishMsgPos, _publishUseIndication, "pub");

        // Wait for new message enqueue or indication ACK (or timeout for pacing)
        // The wait is at least 1 tick so that this task cannot busy-spin
        TickType_t waitTicks = pdMS_TO_TICKS(_minMsBetweenNotifySends);
        if (waitTicks == 0)
            waitTicks = 1;
        if (_outboundSemaphore)
            xSemaphoreTake(_outboundSemaphore, waitTicks);
        else
            vTaskDelay(waitTicks);
    }

    // Task has exited (no members may be accessed after the handle is cleared)
    _outboundMsgTaskHandle = nullptr;
    vTaskDelete(NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Notify tx complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattOutbound::notifyTxComplete(int statusBLEHSCode, bool isIndication)
{
    // This callback fires for both notifications (isIndication=false) and indications (isIndication=true)
    // For notifications: status=0 means sent (no ACK)
    // For indications: status=0 means sent, status=BLE_HS_EDONE(14) means ACK received
    // Only process indication events since _outboundMsgsInFlight only tracks indications
    if (!isIndication)
        return;

    // Decrement messages in flight on ACK received (EDONE) or failure (non-zero, non-sent)
    if (statusBLEHSCode != 0)
    {
        // Decrement messages in flight
        int32_t msgsInFlight = decrementMsgsInFlight();
        _outbountMsgInFlightLastMs = millis();

        // Wake the outbound task to send next queued message immediately
        if (_outboundSemaphore)
            xSemaphoreGive(_outboundSemaphore);
        (msgsInFlight = msgsInFlight); // avoid warning when not debugging

#ifdef DEBUG_SEND_FROM_OUTBOUND_QUEUE
        LOG_I(MODULE_PREFIX, "notifyTxComplete status %s msgsInFlight %d", 
                BLEGattServer::getHSErrorMsg(statusBLEHSCode), msgsInFlight);
#endif
    }
}

#endif // CONFIG_BT_ENABLED
