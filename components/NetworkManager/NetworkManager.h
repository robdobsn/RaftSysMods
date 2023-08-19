/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Network Manager
// Handles state of WiFi system, retries, etc and Ethernet
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <SysModBase.h>
#include "NetworkSystem.h"

class ConfigBase;
class RestAPIEndpointManager;
class APISourceInfo;

class NetworkManager : public SysModBase
{
public:
    NetworkManager(const char* pModuleName, ConfigBase& defaultConfig, ConfigBase* pGlobalConfig, 
                ConfigBase* pMutableConfig);

protected:
    // Setup
    virtual void setup() override final;

    // Service - called frequently
    virtual void service() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

    // Get status JSON
    virtual String getStatusJSON() override final;

    // Get debug string
    virtual String getDebugJSON() override final;

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
};
