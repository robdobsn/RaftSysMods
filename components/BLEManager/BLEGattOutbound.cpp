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

// Debug
// #define DEBUG_SEND_FROM_OUTBOUND_QUEUE
// #define DEBUG_BLE_TX_MSG
// #define DEBUG_BLE_PUBLISH

BLEGattOutbound::BLEGattOutbound(BLEGattServer& gattServer, BLEManStats& bleStats) :
        _gattServer(gattServer),
        _bleStats(bleStats),
        _outboundQueue(DEFAULT_OUTBOUND_MSG_QUEUE_SIZE)
{
    _inFlightMutex = xSemaphoreCreateMutex();
}

BLEGattOutbound::~BLEGattOutbound()
{
    if (_inFlightMutex)
        vSemaphoreDelete(_inFlightMutex);
}

// Setup
bool BLEGattOutbound::setup(uint32_t maxPacketLen, uint32_t outboundQueueSize, bool useTaskForSending,
            UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize,
            bool sendUsingIndication)
{
    // Max len
    _maxPacketLen = maxPacketLen;

    // Send using indication
    _sendUsingIndication = sendUsingIndication;

    // Setup queue
    _outboundQueue.setMaxLen(outboundQueueSize);

    // Check if a thread should be started for sending
    if (useTaskForSending)
    {
        // Start the worker task
        BaseType_t retc = pdPASS;
        if (_outboundMsgTaskHandle == nullptr)
        {
            retc = xTaskCreatePinnedToCore(
                        [](void* pArg){
                            ((BLEGattOutbound*)pArg)->outboundMsgTask();
                        },
                        "BLEOutQ",                              // task name
                        taskStackSize,                          // stack size of task
                        this,                                   // parameter passed to task on execute
                        taskPriority,                           // priority
                        (TaskHandle_t*)&_outboundMsgTaskHandle, // task handle
                        taskCore);                              // pin task to core N
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
        vTaskDelete(_outboundMsgTaskHandle);
        _outboundMsgTaskHandle = nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service outbound queue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattOutbound::serviceOutboundQueue()
{
    // Send if not using alternate thread
    if (_outboundMsgTaskHandle == nullptr)
        handleSendFromOutboundQueue();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattOutbound::isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
{
    // Only send PUBLISH messages if nothing else pending
    if (msgType == MSG_TYPE_PUBLISH)
        return (!_sendUsingIndication || (_outboundMsgsInFlight == 0)) && (_outboundQueue.count() == 0);

    // Check the queue is empty
    return _outboundQueue.count() < _outboundQueue.maxLen();
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

    // Add to the queue
    ProtocolRawMsg bleOutMsg(msg.getBuf(), msg.getBufLen());
    bool putOk = _outboundQueue.put(bleOutMsg);
    if (!putOk)
    {
        LOG_W(MODULE_PREFIX, "sendBLEMsg FAILEDTOSEND totalLen %d", msg.getBufLen());
    }
    return putOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle sending from outbound queue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattOutbound::handleSendFromOutboundQueue()
{
    // When using send with indication we get a confirmation of each packet being sent and this is used to
    // control the rate of sending. When not using indication we send using timed intervals.
    if (_sendUsingIndication)
    {
        // Check if less than the max number of messages in flight
        if (_outboundMsgsInFlight > 0)
        {
            // Check for timeout on in flight messages
            if (Raft::isTimeout(millis(), _outbountMsgInFlightLastMs, BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS))
            {
                // Debug
                LOG_W(MODULE_PREFIX, "loop outbound msg timeout");

                // Timeout so clear the in flight count
                _outboundMsgsInFlight = 0;
            }
            return false;
        }
    }

    // Check time since last send
    if (!_sendUsingIndication)
    {
        if (!Raft::isTimeout(millis(), _lastOutboundMsgMs, BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS))
            return false;
    }

    // Peek next message in queue
    ProtocolRawMsg bleOutMsg;
    if (!_outboundQueue.peek(bleOutMsg))
        return false;

    // Extract next section of message to send
    uint32_t toSendLen = 0;
    bool removeFromQueue = true;
    if (bleOutMsg.getBufLen() > _outboundMsgPos)
        toSendLen = bleOutMsg.getBufLen() - _outboundMsgPos;
    uint32_t maxLen = ((_mtuSize != 0) && (_mtuSize > MTU_SIZE_REDUCTION+1)) ? _mtuSize - MTU_SIZE_REDUCTION : _maxPacketLen;
    if (toSendLen > maxLen)
        toSendLen = maxLen;
    if (bleOutMsg.getBufLen() > _outboundMsgPos + toSendLen)
        removeFromQueue = false;
    BLEGattServerSendResult rslt = BLEGATT_SERVER_SEND_RESULT_TRY_AGAIN;
    if (toSendLen != 0)
    {
        // Handle messages in flight calculation when using indication
        if (_sendUsingIndication)
        {
            _outbountMsgInFlightLastMs = millis();
            if (xSemaphoreTake(_inFlightMutex, pdMS_TO_TICKS(WAIT_FOR_INFLIGHT_MUTEX_MAX_MS)) == pdTRUE)
            {
                _outboundMsgsInFlight = _outboundMsgsInFlight + 1;
                xSemaphoreGive(_inFlightMutex);
            }
        }

        // Send to central
        _lastOutboundMsgMs = millis();
        rslt = _gattServer.sendToCentral(bleOutMsg.getBuf() + _outboundMsgPos, toSendLen);
        if (rslt == BLEGATT_SERVER_SEND_RESULT_OK)
        {
            _bleStats.txMsg(bleOutMsg.getBufLen(), rslt);
            _outboundMsgPos += toSendLen;
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
            removeFromQueue = true;
        }

        // Handle messages in flight calculation when using indication
        if ((rslt != BLEGATT_SERVER_SEND_RESULT_OK) && _sendUsingIndication)
        {
            if (xSemaphoreTake(_inFlightMutex, pdMS_TO_TICKS(WAIT_FOR_INFLIGHT_MUTEX_MAX_MS)) == pdTRUE)
            {
                _outboundMsgsInFlight = _outboundMsgsInFlight - 1;
                xSemaphoreGive(_inFlightMutex);
            }
        }
    }

    // Remove from queue if required
    if (removeFromQueue)
    {
        _outboundQueue.get(bleOutMsg);
        _outboundMsgPos = 0;
    }

#ifdef DEBUG_SEND_FROM_OUTBOUND_QUEUE
    if (_sendUsingIndication)
    {
        uint32_t msgsInFlight = 0;
        if (xSemaphoreTake(_inFlightMutex, pdMS_TO_TICKS(WAIT_FOR_INFLIGHT_MUTEX_MAX_MS)) == pdTRUE)
        {
            msgsInFlight = _outboundMsgsInFlight;
            xSemaphoreGive(_inFlightMutex);
        }
        LOG_I(MODULE_PREFIX, "handleSendFromOutboundQueue sendLen %d totalLen %d msgPos %d sendOk %d inFlight %d leftInQueue %d removeFromQ %d", 
                toSendLen, bleOutMsg.getBufLen(), _outboundMsgPos, rslt, msgsInFlight, _outboundQueue.count(), removeFromQueue);
    }
    else
    {
        LOG_I(MODULE_PREFIX, "handleSendFromOutboundQueue sendLen %d totalLen %d msgPos %d sendOk %s leftInQueue %d removeFromQ %d", 
            toSendLen, bleOutMsg.getBufLen(), _outboundMsgPos, 
            rslt == BLEGATT_SERVER_SEND_RESULT_OK ? "OK" : rslt == BLEGATT_SERVER_SEND_RESULT_FAIL ? "FAIL" : "TRYAGAIN", 
            _outboundQueue.count(), removeFromQueue);
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
    while (ulTaskNotifyTake(pdTRUE, 0) == 0)
    {        
        // Handle queue
        handleSendFromOutboundQueue();

        // Yield
        vTaskDelay(1);
    }

    // Task has exited
    _outboundMsgTaskHandle = nullptr;
    vTaskDelete(NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Notify tx complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattOutbound::notifyTxComplete(int statusBLEHSCode)
{
    // Check if using indications - if so use this notification to handle the in-flight count
    if (_sendUsingIndication)
    {
        // Decrement messages in flight if either DONE or FAIL
        if (statusBLEHSCode != 0)
        {
            uint32_t msgsInFlight = 0;
            if (xSemaphoreTake(_inFlightMutex, pdMS_TO_TICKS(WAIT_FOR_INFLIGHT_MUTEX_MAX_MS)) == pdTRUE)
            {
                // Decrement messages in flight
                if (_outboundMsgsInFlight != 0)
                {
                    // Decrement the in flight count
                    _outboundMsgsInFlight = _outboundMsgsInFlight - 1;
                }
                msgsInFlight = _outboundMsgsInFlight;
                xSemaphoreGive(_inFlightMutex);
            }
            _outbountMsgInFlightLastMs = millis();
            (msgsInFlight = msgsInFlight); // avoid warning when not debugging

#ifdef DEBUG_SEND_FROM_OUTBOUND_QUEUE
            LOG_I(MODULE_PREFIX, "notifyTxComplete status %s msgsInFlight %d", 
                    BLEGattServer::getHSErrorMsg(statusBLEHSCode), msgsInFlight);
#endif
        }
    }
}

#endif // CONFIG_BT_ENABLED
