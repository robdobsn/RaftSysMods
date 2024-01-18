/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEManager
// Handles BLE connectivity and data
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "SysModBase.h"
#include "sdkconfig.h"
#include "BLEGapServer.h"

class CommsChannelMsg;
class APISourceInfo;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BLEManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class BLEManager : public SysModBase
{
public:
    BLEManager(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~BLEManager();

    // Create function (for use by SysManager factory)
    static SysModBase* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new BLEManager(pModuleName, sysConfig);
    }

protected:
    // Setup
    virtual void setup() override final;

    // Service - called frequently
    virtual void service() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;

    // Add comms channel
    virtual void addCommsChannels(CommsCoreIF& commsCoreIF) override final;

    // Get status JSON
    virtual String getStatusJSON() override final;

    // Get debug string
    virtual String getDebugJSON() override final;

    // Get named value
    virtual double getNamedValue(const char* valueName, bool& isValid) override final;

private:

#ifdef CONFIG_BT_ENABLED

    // BLE enabled
    bool _enableBLE = false;

    // BLE Gap server
    BLEGapServer _gapServer;

    // Helpers
    String getAdvertisingName();

    // Restart API
    RaftRetCode apiBLERestart(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
#endif
};
