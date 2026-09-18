/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLE Bus Device Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BLEBusDeviceManager.h"
#include "RaftCore.h"
#include "DeviceTypeRecords.h"
#include "BLEAdvertDecoder.h"
#include "BusAddrStatus.h"

// #define DEBUG_GET_DEVICE_ADDRESSES
// #define DEBUG_GET_DEVICE_DATA_JSON
// #define DEBUG_GET_DEVICE_DATA_BINARY
// #define DEBUG_GET_DEVICE_DATA_TIMESTAMP
// #define DEBUG_HANDLE_POLL_RESULT
// #define DEBUG_GET_DEVICE_STATE_BY_ADDR
// #define DEBUG_GET_DEVICE_JSON_BY_ADDR

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief BLE Bus Device Manager
/// @param raftBus 
BLEBusDeviceManager::BLEBusDeviceManager(RaftBus& raftBus) :
    _raftBus(raftBus)
{
    // Access semaphore
    _accessMutex = xSemaphoreCreateMutex();

    // Get device type info
    deviceTypeRecords.getDeviceInfo("BLEBTHome", _devTypeRec, _deviceTypeIndex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param config configuration
void BLEBusDeviceManager::setup(const RaftJsonIF& config)
{
    // Debug
    LOG_I(MODULE_PREFIX, "BLEBusDeviceManager setup");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop (must be called from the main task)
/// @note Poll results are stored by handlePollResult() on the NimBLE host task. The resulting bus element status
///       and device data change callbacks are made here as they must run on the main task
void BLEBusDeviceManager::loop()
{
    // Check if any callbacks are pending
    if (!_callbacksPending.exchange(false))
        return;

    // Get pending status changes
    std::vector<BusAddrStatus> statusChanges;
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        // Try again next time
        _callbacksPending = true;
        return;
    }
    for (BLEBusDeviceState& devState : _bleBusDeviceStates)
    {
        if (devState.statusCBPending)
        {
            devState.statusCBPending = false;
            statusChanges.push_back(BusAddrStatus(devState.busElemAddr, DeviceOnlineState::ONLINE, true, true, _deviceTypeIndex));
        }
    }
    xSemaphoreGive(_accessMutex);

    // Status callback (made without the mutex held as it may result in a call to registerForDeviceData)
    if (statusChanges.size() > 0)
        _raftBus.callBusElemStatusCB(statusChanges);

    // Get pending data changes - this is done after the status callback so that a data change callback registered
    // in the status callback receives the data which caused the device to be discovered
    struct PendingDataCB
    {
        BusElemAddrType address;
        RaftDeviceDataChangeCB dataChangeCB;
        const void* pCallbackInfo;
        std::vector<uint8_t> data;
    };
    std::vector<PendingDataCB> pendingDataCBs;
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        // Try again next time
        _callbacksPending = true;
        return;
    }
    for (BLEBusDeviceState& devState : _bleBusDeviceStates)
    {
        if (devState.dataCBPending)
        {
            devState.dataCBPending = false;
            if (devState.dataChangeCB)
                pendingDataCBs.push_back({devState.busElemAddr, devState.dataChangeCB, devState.pCallbackInfo, std::move(devState.dataCBData)});
            devState.dataCBData.clear();
        }
    }
    xSemaphoreGive(_accessMutex);

    // Data change callbacks (made without the mutex held)
    for (PendingDataCB& pendingDataCB : pendingDataCBs)
    {
        pendingDataCB.dataChangeCB(pendingDataCB.address, pendingDataCB.data, pendingDataCB.pCallbackInfo);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get list of device addresses attached to the bus
/// @param pAddrList pointer to array to receive addresses
/// @param onlyAddressesWithIdentPollResponses true to only return addresses with poll responses    
void BLEBusDeviceManager::getDeviceAddresses(std::vector<BusElemAddrType>& addresses, bool onlyAddressesWithPollResponses) const
{
    // Obtain semaphore
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return;

    // Iterate device states
    for (const BLEBusDeviceState& devState : _bleBusDeviceStates)
    {
        // Check if only addresses with poll responses
        if (onlyAddressesWithPollResponses && devState.lastDataReceived.size() == 0)
            continue;

        // Add address to list
        addresses.push_back(devState.busElemAddr);
    }

    // Return semaphore
    xSemaphoreGive(_accessMutex);

#ifdef DEBUG_GET_DEVICE_ADDRESSES
    String logStr;
    for (BusElemAddrType addr : addresses)
    {
        logStr += String(addr) + " ";
    }
    LOG_I(MODULE_PREFIX, "getDeviceAddresses %s", logStr.c_str());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get queued device data in JSON format
/// @return JSON doc
String BLEBusDeviceManager::getQueuedDeviceDataJson()
{
    // Return string
    String jsonStr;

    // Get semaphore
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return "{}";

    // Iterate list of devices
    for (BLEBusDeviceState& devState : _bleBusDeviceStates)
    {
        // Get poll response JSON
        if (devState.lastDataReceived.size() > 0)
        {
            String pollResponseJson = deviceStatusToJson(devState.busElemAddr, DeviceOnlineState::ONLINE, _deviceTypeIndex, 
                            devState.lastDataReceived, devState.lastDataReceived.size());
            if (pollResponseJson.length() > 0)
            {
                jsonStr += (jsonStr.length() == 0 ? "{" : ",") + pollResponseJson;
            }

            // Clear data
            devState.lastDataReceived.clear();
        }
    }

    // Return semaphore
    xSemaphoreGive(_accessMutex);

    // Debug
#ifdef DEBUG_GET_DEVICE_DATA_JSON
    LOG_I(MODULE_PREFIX, "getQueuedDeviceDataJson %s", (jsonStr.length() == 0 ? "{}" : jsonStr + "}").c_str());
#endif

    // Return JSON
    return jsonStr.length() == 0 ? "{}" : jsonStr + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get queued device data in binary format
/// @param busNumber bus number
/// @return Binary data vector
std::vector<uint8_t> BLEBusDeviceManager::getQueuedDeviceDataBinary(uint32_t busNumber)
{
    // Binary data
    std::vector<uint8_t> binaryData;

    // Get semaphore
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return binaryData;

    // Iterate list of devices
    for (BLEBusDeviceState& devState : _bleBusDeviceStates)
    {
        // Get poll response JSON
        if (devState.lastDataReceived.size() > 0)
        {
            // Build length-prefixed sample payload
            std::vector<uint8_t> payload;
            uint16_t dataLen = devState.lastDataReceived.size();
            if (dataLen > 255) dataLen = 255;
            payload.push_back(static_cast<uint8_t>(dataLen));
            payload.insert(payload.end(), devState.lastDataReceived.begin(), 
                           devState.lastDataReceived.begin() + dataLen);

            // Generate binary device record with pre-formatted length-prefixed payload
            RaftDevice::genBinaryDeviceRecord(binaryData, busNumber, devState.busElemAddr, _deviceTypeIndex, DeviceOnlineState::ONLINE, 0, payload);

            // Clear data
            devState.lastDataReceived.clear();
        }
    }

    // Return semaphore
    xSemaphoreGive(_accessMutex);

    // Debug
#ifdef DEBUG_GET_DEVICE_DATA_BINARY
    LOG_I(MODULE_PREFIX, "getQueuedDeviceDataBinary len %d", binaryData.size());
#endif

    // Return binary data
    return binaryData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get latest timestamp of change to device info (online/offline, new data, etc)
/// @param includeElemOnlineStatusChanges include changes in online status of elements
/// @param includeDeviceDataUpdates include new data updates
/// @return timestamp of most recent device info in ms
uint32_t BLEBusDeviceManager::getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includeDeviceDataUpdates) const
{
    // Check if any updates required
    if (!includeDeviceDataUpdates)
        return 0;

#ifdef DEBUG_GET_DEVICE_DATA_TIMESTAMP
    LOG_I(MODULE_PREFIX, "getDeviceInfoTimestampMs %d", (int)_deviceDataLastSetMs.load());
#endif

    // Return time of last set
    return _deviceDataLastSetMs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle poll results
/// @param timeNowUs time in us (passed in to aid testing)
/// @param address address
/// @param pollResultData poll result data
/// @param pPollInfo pointer to device polling info (maybe nullptr) 
/// @return true if result stored
bool BLEBusDeviceManager::handlePollResult(uint64_t timeNowUs, BusElemAddrType address, 
                        const std::vector<uint8_t>& pollResultData, const DevicePollingInfo* pPollInfo)
{
    // Ms time
    uint32_t timeNowMs = timeNowUs / 1000;

    // Get the deviceID
    uint8_t deviceID = 0;
    bool isFirst = false;
#ifdef CONFIG_BT_ENABLED
    if (pollResultData.size() > 1)
    {
        deviceID = pollResultData[BLEAdvertDecoder::DUPLICATE_RECORD_DEVICE_ID_POS];
    }
#endif

    // Obtain semaphore
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return false;

    // Check if device already seen
    BLEBusDeviceState* pDevState = getBLEBusDeviceState(address);
    if ((pDevState == nullptr) && (_bleBusDeviceStates.size() < MAX_BLE_BUS_DEVICES))
    {
        // Create new device state
        BLEBusDeviceState devState;
        devState.busElemAddr = address;
        devState.lastBTHomePacketID = deviceID;

        // The bus element status callback is made from loop() on the main task (this function
        // is called on the NimBLE host task)
        devState.statusCBPending = true;
        _bleBusDeviceStates.push_back(devState);
        isFirst = true;

        // Get the device state
        pDevState = getBLEBusDeviceState(address);

        // Check valid
        if (pDevState == nullptr)
        {
            xSemaphoreGive(_accessMutex);
            return false;
        }
    }

    // Check if device state available
    bool isNotARepeat = isFirst || (pDevState && (pDevState->lastBTHomePacketID != deviceID));
    bool callbacksPending = isFirst;
    if (pDevState && isNotARepeat)
    {
        // Check if the data change callback should be called - the callback is made from loop() on the main task
        // Note that for the first data from a device the callback is not yet registered (it is generally
        // registered in the status callback) so the data is always marked pending in that case
        if (isFirst || pDevState->dataChangeCB)
        {
            // Check if time to report
            if (isFirst || (pDevState->minTimeBetweenReportsMs == 0) || (Raft::isTimeout(timeNowMs, pDevState->lastSeenTimeMs, pDevState->minTimeBetweenReportsMs)))
            {
                // Note that the callback should be called
                pDevState->dataCBData = pollResultData;
                pDevState->dataCBPending = true;
                callbacksPending = true;
            }
        }

        // Store poll results
        pDevState->lastDataReceived = pollResultData;
        _deviceDataLastSetMs = timeNowMs;

        // Update last seen time and packet ID
        pDevState->lastSeenTimeMs = timeNowMs;
        pDevState->lastBTHomePacketID = deviceID;
    }

    // Return semaphore
    xSemaphoreGive(_accessMutex);

    // Flag that callbacks are required (this must be done after the pending data is stored)
    if (callbacksPending)
        _callbacksPending = true;

    // Debug
#ifdef DEBUG_HANDLE_POLL_RESULT
    String pollResultStr;
    Raft::getHexStrFromBytes(pollResultData.data(), pollResultData.size(), pollResultStr);
    LOG_I(MODULE_PREFIX, "handlePollResult %04x %s %s", 
                address, 
                pollResultStr.c_str(), 
                storeReqd ? "STORED" : "NOT_STORED");
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device state
/// @param busElemAddr address of device to get state for
/// @return BLEBusDeviceState* or nullptr
BLEBusDeviceManager::BLEBusDeviceState* BLEBusDeviceManager::getBLEBusDeviceState(BusElemAddrType address)
{
    // Find the device state
    for (auto& devState : _bleBusDeviceStates)
    {
        if (devState.busElemAddr == address)
        {
#ifdef DEBUG_GET_DEVICE_STATE_BY_ADDR
            LOG_I(MODULE_PREFIX, "getBLEBusDeviceState found %04x lastSeenTimeMs %dms ago lastDataLen %d", 
                    address, 
                    Raft::timeElapsed(millis(), devState.lastSeenTimeMs),
                    devState.lastDataReceived.size());
#endif
            return &devState;
        }
    }
#ifdef DEBUG_GET_DEVICE_STATE_BY_ADDR
    LOG_I(MODULE_PREFIX, "getBLEBusDeviceState not found %04x", address);
#endif
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device status JSON
/// @param address address
/// @param onlineState device online state
/// @param deviceTypeIndex index of device type
/// @param devicePollResponseData poll response data
/// @param responseSize size of poll response data
/// @return JSON string
String BLEBusDeviceManager::deviceStatusToJson(BusElemAddrType address, DeviceOnlineState onlineState, uint16_t deviceTypeIndex, 
                const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize) const
{
    // Get the poll response JSON using DeviceOnlineState directly
    String devJson = deviceTypeRecords.deviceStatusToJson(address, onlineState, deviceTypeIndex, devicePollResponseData);

    // Debug
#ifdef DEBUG_GET_DEVICE_JSON_BY_ADDR
    LOG_I(MODULE_PREFIX, "deviceStatusToJson %04x %s %s", address, BusAddrStatus::getOnlineStateStr(onlineState), devJson.c_str());
#endif
    return devJson;
}

