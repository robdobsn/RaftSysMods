/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLE Bus Handler
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftBus.h"
#include "RaftJsonIF.h"
#include "BLEBusDeviceManager.h"

class BusBLE : public RaftBus
{
public:
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Constructor
    /// @param busElemStatusCB - callback for bus element status changes
    /// @param busOperationStatusCB - callback for bus operation status changes
    /// @param pI2CCentralIF - pointer to I2C central interface (if nullptr then use default I2C interface)
    BusBLE(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destructor
    virtual ~BusBLE();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Create function to create a new instance of this class
    /// @param busElemStatusCB - callback for bus element status changes
    /// @param busOperationStatusCB - callback for bus operation status changes
    static RaftBus* createFn(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB)
    {
        return new BusBLE(busElemStatusCB, busOperationStatusCB);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief setup
    /// @param config - configuration
    /// @return true if setup was successful
    virtual bool setup(const RaftJsonIF& config) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus devices interface
    virtual RaftBusDevicesIF* getBusDevicesIF() override final
    {
        return &_bleBusDeviceManager;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus name
    /// @return bus name
    virtual String getBusName() const override final
    {
        return _busName;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief isOperatingOk
    /// @return true if the bus is operating OK
    virtual BusOperationStatus isOperatingOk() const override final
    {
        return BUS_OPERATION_OK;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get latest timestamp of change to device info (online/offline, new data, etc)
    /// @param includeElemOnlineStatusChanges include changes in online status of elements
    /// @param includeDeviceDataUpdates include new data updates
    /// @return timestamp of most recent device info in ms
    virtual uint32_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includeDeviceDataUpdates) const override final
    {
        return _bleBusDeviceManager.getDeviceInfoTimestampMs(includeElemOnlineStatusChanges, includeDeviceDataUpdates);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert bus address to string
    /// @param addr - address
    /// @return address as a string
    virtual String addrToString(BusElemAddrType addr) const override final
    {
        return String(addr);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert string to bus address
    /// @param addrStr - address as a string
    /// @return address
    virtual BusElemAddrType stringToAddr(const String& addrStr) const override final
    {
        return strtol(addrStr.c_str(), NULL, 0);
    }

private:

    // Settings
    String _busName;

    // BLE data source manager
    BLEBusDeviceManager _bleBusDeviceManager;

    // Debug
    uint32_t _debugLastBusLoopMs = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "BLEBus";
};
