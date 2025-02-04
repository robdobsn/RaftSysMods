/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEManager
// Handles BLE connectivity and data
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "sdkconfig.h"
#include "BLEGapServer.h"

class CommsChannelMsg;
class APISourceInfo;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BLEManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class BLEManager : public RaftSysMod
{
public:
    BLEManager(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~BLEManager();

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new BLEManager(pModuleName, sysConfig);
    }

protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;

    // Add comms channel
    virtual void addCommsChannels(CommsCoreIF& commsCoreIF) override final;

    // Get status JSON
    virtual String getStatusJSON() const override final;

    // Get debug string
    virtual String getDebugJSON() const override final;

    // Get named value
    virtual double getNamedValue(const char* valueName, bool& isValid) override final;

    // Set named value
    virtual bool setNamedValue(const char* valueName, double value) override final;

private:

#ifdef CONFIG_BT_ENABLED

    // BLE enabled
    bool _enableBLE = false;

    // BLE Gap server
    BLEGapServer _gapServer;

    // Manufacturer data
    std::vector<uint8_t> _advManufSerialNo;
    static const uint32_t MAX_MANUFACTURER_ID_LEN = 2;
    static const uint32_t MAX_MANUFACTURER_DATA_LEN = 8;

    // Helpers
    String getAdvertisingName();
    void getAdvertisingData(std::vector<uint8_t>& manufacturerData);
    
    // Restart API
    RaftRetCode apiBLERestart(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
#endif

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "BLEMan";
};
