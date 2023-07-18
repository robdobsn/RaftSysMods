/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGattOutbound - Outbound GATT
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BLEGattOutbound.h"
#include <CommsChannelMsg.h>
#include <Logger.h>
#include <RaftUtils.h>
#include "BLEGattServer.h"
#include "BLEManStats.h"

// #define DEBUG_SEND_FROM_OUTBOUND_QUEUE

static const char* MODULE_PREFIX = "BLEOut";

BLEGattOutbound::BLEGattOutbound(BLEGattServer& gattServer, BLEManStats& bleStats) :
        _gattServer(gattServer),
        _bleStats(bleStats),
        _bleFragmentQueue(DEFAULT_OUTBOUND_MSG_QUEUE_SIZE)
{
}

BLEGattOutbound::~BLEGattOutbound()
{
}

// Setup
bool BLEGattOutbound::setup(uint32_t maxPacketLen, uint32_t outboundQueueSize, bool useTaskForSending,
            UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize)
{
    // Max len
    _maxPacketLen = maxPacketLen;

    // Setup queue
    _bleFragmentQueue.setMaxLen(outboundQueueSize);

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

void BLEGattOutbound::service()
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
    {
        if (!_outboundMsgInFlight)
        {
            handleSendFromOutboundQueue();
        }
        else
        {
            // Check for timeout on in flight message
            if (Raft::isTimeout(millis(), _outbountMsgInFlightStartMs, BLE_OUTBOUND_MSG_IN_FLIGHT_TIMEOUT_MS))
            {
                // Debug
                LOG_W(MODULE_PREFIX, "service outbound msg timeout");

                // Timeout so send again
                _outboundMsgInFlight = false;
                handleSendFromOutboundQueue();
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattOutbound::isReadyToSend(uint32_t channelID, bool& noConn)
{
    // Check the queue is empty
    if (_outboundMsgInFlight || (_bleFragmentQueue.count() > 0))
        return false;
    return true;
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

    // Check if publish message and only add if outbound queue is empty
    if ((msg.getMsgTypeCode() == MSG_TYPE_PUBLISH) && (_bleFragmentQueue.count() != 0))
        return false;

    // Split into multiple messages if required
    const uint8_t* pMsgPtr = msg.getBuf();
    uint32_t remainingLen = msg.getBufLen();
    for (int msgIdx = 0; msgIdx < _bleFragmentQueue.maxLen(); msgIdx++)
    {
        uint32_t msgLen = _maxPacketLen;
        if (msgLen > remainingLen)
            msgLen = remainingLen;

        // Send to the queue
        ProtocolRawMsg bleOutMsg(pMsgPtr, msgLen);
        bool putOk = _bleFragmentQueue.put(bleOutMsg);

#ifdef DEBUG_BLE_TX_MSG_SPLIT
        if (msg.getBufLen() > _maxPacketLen) {
            const char* pDebugHex = nullptr;
#ifdef DEBUG_BLE_TX_MSG_DETAIL
            String hexStr;
            Raft::getHexStrFromBytes(pMsgPtr, msgLen, hexStr);
            pDebugHex = hexStr.c_str();
#endif
            LOG_W(MODULE_PREFIX, "sendBLEMsg msgIdx %d partOffset %d partLen %d totalLen %d remainLen %d putOk %d %s", 
                    msgIdx, pMsgPtr-msg.getBuf(), msgLen, msg.getBufLen(), remainingLen, putOk,
                    pDebugHex ? pDebugHex : "");
        }
#endif
        // Check ok
        if (!putOk)
            break;

        // Calculate remaining
        remainingLen -= msgLen;
        pMsgPtr += msgLen;
        if (remainingLen == 0)
            break;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle sending from outbound queue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattOutbound::handleSendFromOutboundQueue()
{
    // Handle outbound message queue
    if (_bleFragmentQueue.count() > 0)
    {
        if (Raft::isTimeout(millis(), _lastOutboundMsgMs, BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS))
        {
            ProtocolRawMsg bleOutMsg;
            if (_bleFragmentQueue.get(bleOutMsg))
            {
                _outbountMsgInFlightStartMs = millis();
                _outboundMsgInFlight = true;
                bool rslt = _gattServer.sendToCentral(bleOutMsg.getBuf(), bleOutMsg.getBufLen());
                if (!rslt)
                {
                    _outboundMsgInFlight = false;
                }
#ifdef DEBUG_SEND_FROM_OUTBOUND_QUEUE
                LOG_I(MODULE_PREFIX, "handleSendFromOutboundQueue len %d sendOk %d inFlight %d leftInQueue %d", 
                            bleOutMsg.getBufLen(), rslt, _outboundMsgInFlight, _bleFragmentQueue.count());
#endif
                _bleStats.txMsg(bleOutMsg.getBufLen(), rslt);
            }
            _lastOutboundMsgMs = millis();
        }
    }
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

