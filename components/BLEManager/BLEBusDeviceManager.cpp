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
String BLEBusDeviceManager::getQueuedDeviceDataJson() const
{
    // Return string
    String jsonStr;

    // Get semaphore
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return "{}";

    // Iterate list of devices
    for (const BLEBusDeviceState& devState : _bleBusDeviceStates)
    {
        // Get poll response JSON
        if (devState.lastDataReceived.size() > 0)
        {
            String pollResponseJson = deviceStatusToJson(devState.busElemAddr, true, _deviceTypeIndex, 
                            devState.lastDataReceived, devState.lastDataReceived.size());
            if (pollResponseJson.length() > 0)
            {
                jsonStr += (jsonStr.length() == 0 ? "{" : ",") + pollResponseJson;
            }

            // Clear data - const cast
            const_cast<BLEBusDeviceState&>(devState).lastDataReceived.clear();
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
/// @return Binary data vector
std::vector<uint8_t> BLEBusDeviceManager::getQueuedDeviceDataBinary(uint32_t connMode) const
{
    // Binary data
    std::vector<uint8_t> binaryData;

    // Get semaphore
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return binaryData;

    // Iterate list of devices
    for (const BLEBusDeviceState& devState : _bleBusDeviceStates)
    {
        // Get poll response JSON
        if (devState.lastDataReceived.size() > 0)
        {
            // Generate binary device message
            RaftDevice::genBinaryDataMsg(binaryData, connMode, devState.busElemAddr, _deviceTypeIndex, true, devState.lastDataReceived);

            // Clear data - const cast
            const_cast<BLEBusDeviceState&>(devState).lastDataReceived.clear();
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
    LOG_I(MODULE_PREFIX, "getDeviceInfoTimestampMs %d", _deviceDataLastSetMs);
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
    if (pollResultData.size() > 1)
    {
        deviceID = pollResultData[BLEAdvertDecoder::DUPLICATE_RECORD_DEVICE_ID_POS];
    }

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
        _bleBusDeviceStates.push_back(devState);
        isFirst = true;

        // Return semaphore
        xSemaphoreGive(_accessMutex);

        // Callback
        _raftBus.callBusElemStatusCB({BusElemAddrAndStatus(address, true, false, true, _deviceTypeIndex)});

        // Obtain semaphore again
        if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
            return false;

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
    RaftDeviceDataChangeCB dataChangeCB = nullptr;
    const void* pCallbackInfo = nullptr;
    if (pDevState && isNotARepeat)
    {
        // Call data change callback if set
        if (pDevState->dataChangeCB)
        {
            // Check if time to report
            if ((pDevState->minTimeBetweenReportsMs == 0) || (Raft::isTimeout(timeNowMs, pDevState->lastSeenTimeMs, pDevState->minTimeBetweenReportsMs)))
            {
                // Note that the callback should be called
                dataChangeCB = pDevState->dataChangeCB;
                pCallbackInfo = pDevState->pCallbackInfo;
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

    // Call data change callback if required
    if (dataChangeCB)
    {
        // Call the callback
        dataChangeCB(address, pollResultData, pCallbackInfo);
    }

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
/// @brief Get device state by address
/// @param busElemAddr address
/// @return BLEBusDeviceState* or nullptr
BLEBusDeviceManager::BLEBusDeviceState* BLEBusDeviceManager::getBLEBusDeviceState(BusElemAddrType busElemAddr)
{
    // Find the device state
    for (auto& devState : _bleBusDeviceStates)
    {
        if (devState.busElemAddr == busElemAddr)
        {
#ifdef DEBUG_GET_DEVICE_STATE_BY_ADDR
            LOG_I(MODULE_PREFIX, "getBLEBusDeviceState found %04x lastSeenTimeMs %dms ago lastDataLen %d", 
                    busElemAddr, 
                    Raft::timeElapsed(millis(), devState.lastSeenTimeMs),
                    devState.lastDataReceived.size());
#endif
            return &devState;
        }
    }
#ifdef DEBUG_GET_DEVICE_STATE_BY_ADDR
    LOG_I(MODULE_PREFIX, "getBLEBusDeviceState not found %04x", busElemAddr);
#endif
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device status JSON
/// @param address address
/// @param isOnline true if device is online
/// @param deviceTypeIndex index of device type
/// @param devicePollResponseData poll response data
/// @param responseSize size of poll response data
/// @return JSON string
String BLEBusDeviceManager::deviceStatusToJson(BusElemAddrType address, bool isOnline, uint16_t deviceTypeIndex, 
                const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize) const
{
    // Get the poll response JSON
    String devJson = deviceTypeRecords.deviceStatusToJson(address, isOnline, &_devTypeRec, devicePollResponseData);

    // Debug
#ifdef DEBUG_GET_DEVICE_JSON_BY_ADDR
    LOG_I(MODULE_PREFIX, "deviceStatusToJson %04x %s %s", address, isOnline ? "ONLINE" : "OFFLINE", devJson.c_str());
#endif
    return devJson;
}

