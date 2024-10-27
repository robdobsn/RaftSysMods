/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLE Bus Device Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BLEBusDeviceManager.h"
#include "DeviceTypeRecords.h"
#include "BusRequestInfo.h"
#include "DeviceStatus.h"
#include "Logger.h"

// #define DEBUG_GET_DEVICE_ADDRESSES
// #define DEBUG_GET_DEVICE_DATA_JSON
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
    LOG_I(MODULE_PREFIX, "getQueuedDeviceDataJson %s", (jsonStr + "}").c_str());
#endif

    // Return JSON
    return jsonStr.length() == 0 ? "{}" : jsonStr + "}";
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

    // Obtain semaphore
    if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return false;

    // Get the deviceID
    uint8_t deviceID = 0;
    bool isFirst = false;
    if (pollResultData.size() > 1)
    {
        deviceID = pollResultData[0];
    }

    // Check if device already seen
    BLEBusDeviceState* pDevState = getBLEBusDeviceState(address);
    if (pDevState == nullptr)
    {
        // Check limits
        if (_bleBusDeviceStates.size() < MAX_BLE_BUS_DEVICES)
        {
            // Create new device state
            BLEBusDeviceState devState;
            devState.busElemAddr = address;
            devState.lastBTHomePacketID = deviceID;
            _bleBusDeviceStates.push_back(devState);
            pDevState = &_bleBusDeviceStates.back();
            isFirst = true;
        }
    }

    // Check if device state available
    bool storeReqd = isFirst || (pDevState && (pDevState->lastBTHomePacketID != deviceID));
    if (pDevState && storeReqd)
    {
        // Store poll results
        pDevState->lastDataReceived = pollResultData;
        pDevState->lastSeenTimeMs = timeNowMs;
        pDevState->lastBTHomePacketID = deviceID;
        _deviceDataLastSetMs = timeNowMs;
    }

    // Return semaphore
    xSemaphoreGive(_accessMutex);

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

