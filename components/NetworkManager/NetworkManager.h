/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Network Manager
// Handles state of WiFi system, retries, etc and Ethernet
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "NetworkSystem.h"

class RaftJsonIF;
class RestAPIEndpointManager;
class APISourceInfo;

class NetworkManager : public RaftSysMod
{
public:
    NetworkManager(const char* pModuleName, RaftJsonIF& sysConfig);

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new NetworkManager(pModuleName, sysConfig);
    }
    
protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

    // Get status JSON
    virtual String getStatusJSON() const override final;

    // Get debug string
    virtual String getDebugJSON() const override final;

    // Get named value
    virtual double getNamedValue(const char* valueName, bool& isValid);

private:
    // Singleton NetworkManager
    static NetworkManager* _pNetworkManager;

    // Last connection status
    bool _prevConnectedWithIP = false;
    
    // Helpers
    RaftRetCode apiWifiSTASet(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiWifiAPSet(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiWifiClear(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiWiFiPause(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiWifiScan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "NetMan";
};
