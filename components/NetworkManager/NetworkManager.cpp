/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Network Manager
// Handles state of WiFi system, retries, etc and Ethernet
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "NetworkManager.h"
#include "RaftUtils.h"
#include "ConfigNVS.h"
#include "RestAPIEndpointManager.h"
#include "SysManager.h"

// Log prefix
static const char *MODULE_PREFIX = "NetMan";

// Singleton network manager
NetworkManager* NetworkManager::_pNetworkManager = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NetworkManager::NetworkManager(const char *pModuleName, ConfigBase &defaultConfig, ConfigBase *pGlobalConfig, 
            ConfigBase *pMutableConfig)
    : SysModBase(pModuleName, defaultConfig, pGlobalConfig, pMutableConfig)
{
    // Singleton
    _pNetworkManager = this;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkManager::setup()
{
    // Network settings
    NetworkSettings networkSettings;
    networkSettings.setFromConfig(configGetConfig(), _defaultHostname.c_str());

    // Check if we have a friendly name
    bool friendlyNameSet = false;
    String friendlyName = getSysManager()->getFriendlyName(friendlyNameSet);
    if (!friendlyName.isEmpty())
        networkSystem.setHostname(friendlyName.c_str());

    // Setup network system
    bool setupOk = networkSystem.setup(networkSettings);

    // Debug
    LOG_I(MODULE_PREFIX, "setup network %s %s", 
            setupOk ? "OK" : "FAILED",
            networkSystem.getSettingsJSON(false).c_str());

    // Check if setup failed
    if (!setupOk)
        return;

    // Setup WiFi STA
    if (networkSettings.enableWifiSTAMode)
    {
        // Get Wifi STA SSID and password
        String ssid = configGetString("wifiSSID", configGetString("WiFiSSID", ""));
        String password = configGetString("wifiPW", configGetString("WiFiPass", ""));
        if (!ssid.isEmpty())
        {
            bool rsltOk = networkSystem.configWifiSTA(ssid, password);
            LOG_I(MODULE_PREFIX, "setup WiFi STA %s SSID %s", 
                rsltOk ? "OK" : "FAILED",
                ssid.c_str());
        }
    }

    // Setup WiFi AP
    if (networkSettings.enableWifiAPMode)
    {
        String apSSID = configGetString("wifiAPSSID", configGetString("wifiAPSSID", ""));
        String apPassword = configGetString("WiFiAPPass", "");
        if (!apSSID.isEmpty())
        {
            bool rsltOk = networkSystem.configWifiAP(apSSID, apPassword);
            LOG_I(MODULE_PREFIX, "setup WiFi AP %s SSID %s",
                rsltOk ? "OK" : "FAILED",
                apSSID.c_str());
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkManager::service()
{
    // Service network system
    networkSystem.service();

    // Check for status change
    bool isConnWithIP = networkSystem.isIPConnected();
    if (_prevConnectedWithIP != isConnWithIP)
    {
        // Inform hooks of status change
        if (_pNetworkManager)
            _pNetworkManager->executeStatusChangeCBs(isConnWithIP);
        _prevConnectedWithIP = isConnWithIP;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get status JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String NetworkManager::getStatusJSON()
{
    String statusStr = R"({"rslt":"ok")";
    statusStr += R"(,"v":)" + String(getSysManager() ? getSysManager()->getSystemVersion() : "0.0.0");
    statusStr += networkSystem.getConnStateJSON(false, true, true, true, true);
    statusStr += R"(})";
    return statusStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String NetworkManager::getDebugJSON()
{
    return networkSystem.getConnStateJSON(true, true, true, true, false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GetNamedValue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double NetworkManager::getNamedValue(const char* valueName, bool& isValid)
{
    switch(valueName[0])
    {
        case 'R': { return networkSystem.getRSSI(isValid); }
        default: { isValid = false; return 0; }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkManager::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    endpointManager.addEndpoint("w", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                          std::bind(&NetworkManager::apiWifiSTASet, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                          "Setup WiFi STA e.g. w/SSID/password");
    endpointManager.addEndpoint("wap", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                          std::bind(&NetworkManager::apiWifiAPSet, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                          "Setup WiFi AP e.g. wap/SSID/password");
    endpointManager.addEndpoint("wc", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                          std::bind(&NetworkManager::apiWifiClear, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                          "Clear WiFi settings");
    // endpointManager.addEndpoint("wax", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
    //                       std::bind(&NetworkManager::apiWifiExtAntenna, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
    //                       "Set external WiFi Antenna");
    // endpointManager.addEndpoint("wai", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
    //                       std::bind(&NetworkManager::apiWifiIntAntenna, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
    //                       "Set internal WiFi Antenna");
    endpointManager.addEndpoint("wifipause", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
            std::bind(&NetworkManager::apiWiFiPause, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
            "WiFi pause, wifipause/pause, wifipause/resume");
    endpointManager.addEndpoint("wifiscan", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                          std::bind(&NetworkManager::apiWifiScan, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                          "Scan WiFi networks - wifiscan/start - wifiscan/results");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi STA set
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::apiWifiSTASet(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // LOG_I(MODULE_PREFIX, "apiWifiSTASet incoming %s", reqStr.c_str());

    // Get SSID - note that ? is valid in SSIDs so don't split on ? character
    String ssid = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1, false);
    // Get pw - as above
    String pw = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2, false);
    // LOG_I(MODULE_PREFIX, "WiFi PW length %d PW %s", pw.length(), pw.c_str());

    // Check valid
    String errorStr;
    bool configRslt = false;
    if (!ssid.isEmpty())
    {
        // Configure WiFi
        configRslt = networkSystem.configWifiSTA(ssid, pw);
        if (!configRslt)
            errorStr = "configWifiSTA failed";

        // Set hostname if specified
        LOG_I(MODULE_PREFIX, "apiWifiSTASet %s SSID %s (len %d)", 
                    configRslt ? "OK" : "FAIL",
                    ssid.c_str(), ssid.length());
    }
    else
    {
        LOG_I(MODULE_PREFIX, "apiWifiSTASet no SSID specified");
        errorStr = "No SSID specified";
    }

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, configRslt, errorStr.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi AP set
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::apiWifiAPSet(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Get SSID - note that ? is valid in SSIDs so don't split on ? character
    String ssid = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1, false);
    // Get pw - as above
    String pw = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2, false);

    // Debug
    if (ssid.length() > 0)
    {
        // Configure WiFi AP
        bool rslt = networkSystem.configWifiAP(ssid, pw);
        LOG_I(MODULE_PREFIX, "apiWifiAPSet %s SSID %s (len %d)", 
                    rslt ? "OK" : "FAIL",
                    ssid.c_str(), ssid.length());
        String errorStr;
        if (!rslt)
            errorStr = "configWifiAP failed";
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt, errorStr.c_str());
    }
    LOG_I(MODULE_PREFIX, "apiWifiAPSet SSID not specified");
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, false, "No SSID specified");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi clear
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::apiWifiClear(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // See if system restart required
    String sysRestartStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    bool sysRestart = !sysRestartStr.equalsIgnoreCase("norestart");

    // Clear stored credentials back to default
    esp_err_t err = networkSystem.clearCredentials();

    // Debug
    LOG_I(MODULE_PREFIX, "apiWifiClear ResultOK %s", err == ESP_OK ? "Y" : "N");

    // Response
    if (err == ESP_OK)
    {
        Raft::setJsonResult(reqStr.c_str(), respStr, true, nullptr, sysRestart ? R"("norestart":1)" : R"("norestart":0)");

        // Request a system restart
        if (sysRestart && getSysManager())
            getSysManager()->systemRestart();
        return RAFT_OK;
    }
    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, esp_err_to_name(err));
}

// RaftRetCode NetworkManager::apiWifiExtAntenna(String &reqStr, String &respStr)
// {
//     LOG_I(MODULE_PREFIX, "Set external antenna - not supported");
//     return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
// }

// RaftRetCode NetworkManager::apiWifiIntAntenna(String &reqStr, String &respStr)
// {
//     LOG_I(MODULE_PREFIX, "Set internal antenna - not supported");
//     return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
// }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Control WiFi pause on BLE connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::apiWiFiPause(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Get pause arg
    String arg = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1, false);

    // Check
    if (arg.equalsIgnoreCase("pause"))
    {
        networkSystem.pauseWiFi(true);
    }
    else if (arg.equalsIgnoreCase("resume"))
    {
        networkSystem.pauseWiFi(false);
    }
    String pauseJSON = "\"isPaused\":" + String(networkSystem.isPaused() ? "1" : "0");
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, pauseJSON.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan WiFi
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::apiWifiScan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    LOG_I(MODULE_PREFIX, "apiWifiScan %s", reqStr.c_str());

    // Get arg
    String arg = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1, false);

    // Scan WiFi
    String jsonResult;
    bool rslt = networkSystem.wifiScan(arg.equalsIgnoreCase("start"), jsonResult);
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt, jsonResult.c_str());
}
