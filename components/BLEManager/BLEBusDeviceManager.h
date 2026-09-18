/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLE Data Source Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include "Logger.h"
#include "RaftBusDevicesIF.h"
#include "DeviceTypeRecords.h"
#include "RaftThreading.h"

class RaftJsonIF;
class DevicePollingInfo;
class RaftBusDeviceDecodeState;
class RaftBus;

class BLEBusDeviceManager : public RaftBusDevicesIF
{
public:
    BLEBusDeviceManager(RaftBus& raftBus);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Setup
    /// @param config configuration
    void setup(const RaftJsonIF& config);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Loop (must be called from the main task)
    /// @note Raises bus element status and device data change callbacks for results stored by handlePollResult()
    void loop();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get list of device addresses attached to the bus
    /// @param pAddrList pointer to array to receive addresses
    /// @param onlyAddressesWithIdentPollResponses true to only return addresses with ident poll responses    
    virtual void getDeviceAddresses(std::vector<BusElemAddrType>& addresses, bool onlyAddressesWithIdentPollResponses) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by address
    /// @param address address of device to get information for
    /// @param includePlugAndPlayInfo true to include plug and play information
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    virtual String getDevTypeInfoJsonByAddr(BusElemAddrType address, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const override final
    {
        // Get device type info
        deviceTypeIndex = _deviceTypeIndex;
        return deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(_deviceTypeIndex, includePlugAndPlayInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by device type name
    /// @param deviceType device type name
    /// @param includePlugAndPlayInfo true to include plug and play information
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    virtual String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const override final
    {
        // Get device type info
        return deviceTypeRecords.getDevTypeInfoJsonByTypeName(deviceType, includePlugAndPlayInfo, deviceTypeIndex);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type index
    /// @param deviceTypeIdx device type index
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    virtual String getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIdx, bool includePlugAndPlayInfo) const override final
    {
        // Get device type info
        return deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIdx, includePlugAndPlayInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get queued device data in JSON format
    /// @return JSON string
    virtual String getQueuedDeviceDataJson() override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get queued device data in binary format
    /// @param busNumber bus number
    /// @return Binary data vector
    virtual std::vector<uint8_t> getQueuedDeviceDataBinary(uint32_t busNumber) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get decoded poll responses
    /// @param address address of device to get data from
    /// @param pStructOut pointer to structure (or array of structures) to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param maxRecCount maximum number of records to decode
    /// @param decodeState decode state for this device
    /// @return number of records decoded
    /// @note the pStructOut should generally point to structures of the correct type for the device data and the
    ///       decodeState should be maintained between calls for the same device
    virtual uint32_t getDecodedPollResponses(BusElemAddrType address, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const override final
    {
        return 0;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register for device data notifications
    /// @param addrAndSlot address
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    /// @note The callback is called from loop() on the main task
    virtual void registerForDeviceData(BusElemAddrType address, RaftDeviceDataChangeCB dataChangeCB, 
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo) override final
    {
        // Obtain semaphore (device states are also accessed from the NimBLE host task)
        if (xSemaphoreTake(_accessMutex, pdMS_TO_TICKS(REGISTER_MUTEX_MAX_WAIT_MS)) != pdTRUE)
        {
            LOG_W(MODULE_PREFIX, "registerForDeviceData failed to obtain mutex");
            return;
        }

        // Get device state
        BLEBusDeviceState* pDevState = getBLEBusDeviceState(address);
        if (pDevState)
        {
            pDevState->dataChangeCB = dataChangeCB;
            pDevState->minTimeBetweenReportsMs = minTimeBetweenReportsMs;
            pDevState->pCallbackInfo = pCallbackInfo;
        }

        // Return semaphore
        xSemaphoreGive(_accessMutex);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Unregister for device data notifications for a specific address
    /// @param address address
    /// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
    /// @return true if a matching registration was found (and removed)
    /// @note Callbacks are made from loop() on the main task so (when this is called from the main task)
    ///       the callback cannot be in progress and will not be called again after this returns
    virtual bool unregisterForDeviceData(BusElemAddrType address, const void* pCallbackInfo) override final
    {
        return unregisterForDeviceDataHelper(true, address, pCallbackInfo) > 0;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Unregister for device data notifications on all addresses
    /// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
    /// @return number of registrations removed
    /// @note see unregisterForDeviceData
    virtual uint32_t unregisterForDeviceDataAll(const void* pCallbackInfo) override final
    {
        return unregisterForDeviceDataHelper(false, 0, pCallbackInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Handle poll results
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param address address
    /// @param pollResultData poll result data
    /// @param pPollInfo pointer to device polling info (maybe nullptr) 
    /// @return true if result stored
    /// @note This is called on the NimBLE host task so it only stores the result - callbacks are made from loop()
    virtual bool handlePollResult(uint64_t timeNowUs, BusElemAddrType address, 
                            const std::vector<uint8_t>& pollResultData, const DevicePollingInfo* pPollInfo) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get debug JSON
    /// @return JSON string
    virtual String getDebugJSON(bool includeBraces) const override final
    {
        return "{}";
    }

    // /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // /// @brief Identify device
    // /// @param 
    // /// @param deviceStatus (out) device status
    // void identifyDevice(BusElemAddrType address, DeviceStatus& deviceStatus);

    // /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // /// @brief Check device type match (communicates with the device to check its type)
    // /// @param address address
    // /// @param pDevTypeRec device type record
    // /// @return true if device type matches
    // bool checkDeviceTypeMatch(BusElemAddrType address, const DeviceTypeRecord* pDevTypeRec);

    // /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // /// @brief Process device initialisation
    // /// @param address address
    // /// @param pDevTypeRec device type record
    // /// @return true if device initialisation was successful
    // bool processDeviceInit(BusElemAddrType address, const DeviceTypeRecord* pDevTypeRec);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Return addresses of devices attached to the bus
    /// @param addresses - vector to store the addresses of devices
    /// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
    /// @return true if there are any ident poll responses available
    bool getBusElemAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get latest timestamp of change to device info (online/offline, new data, etc)
    /// @param includeElemOnlineStatusChanges include changes in online status of elements
    /// @param includeDeviceDataUpdates include new data updates
    /// @return timestamp of most recent device info in ms
    uint32_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includeDeviceDataUpdates) const;

private:

    // Bus
    RaftBus& _raftBus;

    // Access mutex
    SemaphoreHandle_t _accessMutex = nullptr;

    // BLE Bus Device state
    class BLEBusDeviceState
    {
    public:
        BusElemAddrType busElemAddr = 0;
        uint32_t lastSeenTimeMs = 0;
        uint16_t lastBTHomePacketID = UINT16_MAX;
        std::vector<uint8_t> lastDataReceived;
        RaftDeviceDataChangeCB dataChangeCB = nullptr;
        uint32_t minTimeBetweenReportsMs = 1000;
        const void* pCallbackInfo = nullptr;

        // Callbacks pending (set in handlePollResult() and actioned in loop() on the main task)
        bool statusCBPending = false;
        bool dataCBPending = false;
        std::vector<uint8_t> dataCBData;
    };
    static const uint32_t MAX_BLE_BUS_DEVICES = 20;
    std::list<BLEBusDeviceState> _bleBusDeviceStates;

    // Time of last device data change (written on the NimBLE host task)
    std::atomic<uint32_t> _deviceDataLastSetMs{0};

    // Flag indicating callbacks are pending (set on the NimBLE host task and actioned in loop() on the main task)
    std::atomic<bool> _callbacksPending{false};

    // Max wait for mutex when registering for device data
    static const uint32_t REGISTER_MUTEX_MAX_WAIT_MS = 100;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Helper for unregistering for device data notifications
    /// @param matchAddress true to only unregister for the specified address
    /// @param address address (used if matchAddress is true)
    /// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
    /// @return number of registrations removed
    uint32_t unregisterForDeviceDataHelper(bool matchAddress, BusElemAddrType address, const void* pCallbackInfo)
    {
        // Obtain semaphore (device states are also accessed from the NimBLE host task) - wait indefinitely
        // as the subscriber may be destroyed after this returns so failing to unregister is not an option
        if (xSemaphoreTake(_accessMutex, portMAX_DELAY) != pdTRUE)
            return 0;
        uint32_t numRemoved = 0;
        for (BLEBusDeviceState& devState : _bleBusDeviceStates)
        {
            if (!devState.dataChangeCB || (devState.pCallbackInfo != pCallbackInfo))
                continue;
            if (matchAddress && (devState.busElemAddr != address))
                continue;
            devState.dataChangeCB = nullptr;
            devState.pCallbackInfo = nullptr;
            devState.dataCBPending = false;
            devState.dataCBData.clear();
            numRemoved++;
        }
        xSemaphoreGive(_accessMutex);
        return numRemoved;
    }

    // Device type info - common to all BLE devices
    DeviceTypeRecord _devTypeRec;
    DeviceTypeIndexType _deviceTypeIndex = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Format device status to JSON
    /// @param address address
    /// @param onlineState device online state
    /// @param deviceTypeIndex index of device type
    /// @param devicePollResponseData poll response data
    /// @param responseSize size of poll response data
    /// @return JSON string
    String deviceStatusToJson(BusElemAddrType address, DeviceOnlineState onlineState, DeviceTypeIndexType deviceTypeIndex, 
                    const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize) const;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Decode one or more poll responses for a device
    /// @param deviceTypeIndex index of device type
    /// @param pPollBuf buffer containing poll responses
    /// @param pollBufLen length of poll response buffer
    /// @param pStructOut pointer to structure (or array of structures) to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param maxRecCount maximum number of records to decode
    /// @return number of records decoded
    uint32_t decodePollResponses(DeviceTypeIndexType deviceTypeIndex, 
                    const uint8_t* pPollBuf, uint32_t pollBufLen, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const;


    /// @brief Get device state
    /// @param addresss of device to get state for
    /// @return BLEBusDeviceState* or nullptr if not found
    BLEBusDeviceState* getBLEBusDeviceState(BusElemAddrType address);

    // Debug
    static constexpr const char* MODULE_PREFIX = "BLEBusDevMan";

};
