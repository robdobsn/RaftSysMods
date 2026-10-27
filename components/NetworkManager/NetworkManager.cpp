/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Network Manager
// Handles state of WiFi system, retries, etc and Ethernet
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "Logger.h"
#include "NetworkManager.h"
#include "RaftUtils.h"
#include "RestAPIEndpointManager.h"
#include "SysManager.h"

// Singleton network manager
NetworkManager* NetworkManager::_pNetworkManager = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NetworkManager::NetworkManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig),
      _deviceNetConfig("netman")
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
    networkSettings.setFromConfig(configGetConfig(), "");

    // Static IP set for this device (wifiip API) overrides the SysType
    getStaticIPSetting(networkSettings.wifiStaticIP, networkSettings.wifiStaticGateway, networkSettings.wifiStaticNetmask,
                networkSettings.wifiStaticDNS);

    // Check if we have a friendly name
    bool friendlyNameSet = false;
    String friendlyName = getFriendlyName(friendlyNameSet);
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

    // Apply WiFi credentials from config (when WiFi is started on demand this is done when it starts)
    if (networkSystem.isWifiStarted())
        applyConfiguredCredentials();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Apply WiFi credentials from config (once WiFi is started)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkManager::applyConfiguredCredentials()
{
    // Setup WiFi STA
    NetworkSettings networkSettings;
    networkSettings.setFromConfig(configGetConfig(), "");
    if (networkSettings.enableWifiSTAMode)
    {
        // Get Wifi STA SSID and password
        String ssid = configGetString("wifiSSID", configGetString("WiFiSSID", ""));
        String password = configGetString("wifiPW", configGetString("WiFiPass", ""));
        if (!ssid.isEmpty())
        {
            // Applied on every WiFi start - so only if the stored credentials differ (a reconnect for the same
            // credentials would waste time and forget the fast reconnect AP)
            bool rsltOk = networkSystem.configWifiSTA(ssid, password, true);
            LOG_I(MODULE_PREFIX, "setup WiFi STA %s SSID %s",
                rsltOk ? "OK" : "FAILED",
                ssid.c_str());
        }
    }

    // Setup WiFi AP
    if (networkSettings.enableWifiAPMode)
    {
        String apSSID = configGetString("wifiAPSSID", configGetString("WiFiAPSSID", ""));
        String apPassword = configGetString("wifiAPPW", configGetString("WiFiAPPass", ""));
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
// Start and stop WiFi (on demand)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkManager::startWiFi(bool fullScan)
{
    if (networkSystem.isWifiStarted())
        return true;

    // The static IP setting as it is now (wifiip may have changed it since the last start)
    String ip, gateway, netmask, dns;
    getStaticIPSetting(ip, gateway, netmask, dns);
    networkSystem.setWifiStaticIP(ip, gateway, netmask, dns);
    if (!networkSystem.wifiStart(fullScan))
        return false;
    applyConfiguredCredentials();
    return true;
}

// The WiFi static IP to use: this device's (wifiip API, kept in NVS) if set, otherwise the SysType's (empty for DHCP)
void NetworkManager::getStaticIPSetting(String& ip, String& gateway, String& netmask, String& dns)
{
    String deviceStaticIP = _deviceNetConfig.getString("wifiStaticIP", "");
    if (!deviceStaticIP.isEmpty())
    {
        ip = deviceStaticIP;
        gateway = _deviceNetConfig.getString("wifiStaticGateway", "");
        netmask = _deviceNetConfig.getString("wifiStaticNetmask", "255.255.255.0");
        dns = _deviceNetConfig.getString("wifiStaticDNS", "");
        return;
    }
    NetworkSettings sysTypeSettings;
    sysTypeSettings.setFromConfig(configGetConfig(), "");
    ip = sysTypeSettings.wifiStaticIP;
    gateway = sysTypeSettings.wifiStaticGateway;
    netmask = sysTypeSettings.wifiStaticNetmask;
    dns = sysTypeSettings.wifiStaticDNS;
}

void NetworkManager::stopWiFi()
{
    networkSystem.wifiStop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkManager::loop()
{
    // Service network system
    networkSystem.loop();

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

String NetworkManager::getStatusJSON() const
{
    bool isValid = false;
    String statusStr = R"({"rslt":"ok")";
    statusStr += R"(,"v":")" + (getSysManagerConst() ? getSysManagerConst()->getNamedString(nullptr, "SystemVersion", isValid) + "\"" : String("0.0.0"));
    statusStr += "," + networkSystem.getConnStateJSON(false, true, true, true, true);
    statusStr += R"(})";
    return statusStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String NetworkManager::getDebugJSON() const
{
    return networkSystem.getConnStateJSON(true, true, true, true, false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GetNamedValue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//   RSSI          - WiFi STA RSSI (dBm) - any name starting with R as before
//   started       - 1 if WiFi is started (or will be when unpaused)
//   connected     - 1 if connected with an IP address (WiFi or Ethernet)
//   connMs        - time (ms) from the latest WiFi start to getting an IP address (0 if not yet)
//   fastReconnect - 1 if the latest WiFi start went straight to the remembered AP (wifiSTAFastReconnect)
//   fastFallbacks - number of times since boot that the remembered AP failed and a scan was made

double NetworkManager::getNamedValue(const char* valueName, bool& isValid)
{
    isValid = true;
    if (strcmp(valueName, "started") == 0)
        return networkSystem.isWifiStarted() ? 1 : 0;
    if (strcmp(valueName, "connected") == 0)
        return networkSystem.isIPConnected() ? 1 : 0;
    if (strcmp(valueName, "connMs") == 0)
        return networkSystem.getWifiConnectMs();
    if (strcmp(valueName, "fastReconnect") == 0)
        return networkSystem.wasFastReconnectUsed() ? 1 : 0;
    if (strcmp(valueName, "fastFallbacks") == 0)
        return networkSystem.getFastReconnectFallbacks();
    if (valueName[0] == 'R')
        return networkSystem.getRSSI(isValid);
    isValid = false;
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Receive JSON command from another SysMod
//   {"cmd":"wifiStart","fullScan":0|1} - start WiFi (fullScan forgets the fast reconnect AP and scans all
//                                        channels for the strongest AP)
//   {"cmd":"wifiStop"}                 - stop WiFi
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::receiveCmdJSON(const char* cmdJSON, String* pRespStr)
{
    RaftJson cmd(cmdJSON);
    String cmdName = cmd.getString("cmd", "");
    if (cmdName.equalsIgnoreCase("wifiStart"))
        return startWiFi(cmd.getBool("fullScan", false)) ? RAFT_OK : RAFT_INVALID_OPERATION;
    if (cmdName.equalsIgnoreCase("wifiStop"))
    {
        stopWiFi();
        return RAFT_OK;
    }
    return RAFT_INVALID_OPERATION;
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
    endpointManager.addEndpoint("wifidiag", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                          std::bind(&NetworkManager::apiWifiDiag, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                          "WiFi link diagnostics - wifidiag returns diagnostics, wifidiag?ps=none|min|max&txPwr=dBm&proto=bgn|bg|b&bw=20|40"
                          "&pingMs=N&pingRecover=N&logS=N&reconnect=1 sets link options");
    endpointManager.addEndpoint("wifistart", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                          std::bind(&NetworkManager::apiWifiStart, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                          "Start WiFi (e.g. when startMode is onDemand), wifistart?fullScan=1 forgets the fast reconnect AP and scans "
                          "all channels for the strongest AP");
    endpointManager.addEndpoint("wifistop", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                          std::bind(&NetworkManager::apiWifiStop, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                          "Stop WiFi");
    endpointManager.addEndpoint("wifiip", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                          std::bind(&NetworkManager::apiWifiIP, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                          "Static IP for this device (from the next WiFi start: wifistop then wifistart, or a restart) e.g. wifiip/192.168.1.30/192.168.1.1[/netmask[/dns]], "
                          "wifiip/clear to use the SysType setting (normally DHCP), wifiip returns the setting");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi start and stop
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::apiWifiStart(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    RaftJson params = RestAPIEndpointManager::getJSONFromRESTRequest(reqStr.c_str(), RestAPIEndpointManager::PARAMS_ONLY);
    bool rslt = startWiFi(params.getBool("fullScan", false));
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);
}

RaftRetCode NetworkManager::apiWifiStop(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    stopWiFi();
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi static IP for this device (kept in NVS, applied from the next WiFi start)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::apiWifiIP(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    String ip = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    if (ip.equalsIgnoreCase("clear"))
    {
        _deviceNetConfig.setJsonDoc("{}");
        LOG_I(MODULE_PREFIX, "apiWifiIP cleared - SysType setting used from the next WiFi start");
    }
    else if (!ip.isEmpty())
    {
        String gateway = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
        String netmask = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 3);
        String dns = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 4);
        esp_ip4_addr_t addr;
        if ((esp_netif_str_to_ip4(ip.c_str(), &addr) != ESP_OK) || (esp_netif_str_to_ip4(gateway.c_str(), &addr) != ESP_OK) ||
            (!netmask.isEmpty() && (esp_netif_str_to_ip4(netmask.c_str(), &addr) != ESP_OK)) ||
            (!dns.isEmpty() && (esp_netif_str_to_ip4(dns.c_str(), &addr) != ESP_OK)))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "invalidAddress");
        String jsonDoc = R"({"wifiStaticIP":")" + ip + R"(","wifiStaticGateway":")" + gateway + R"(")";
        if (!netmask.isEmpty())
            jsonDoc += R"(,"wifiStaticNetmask":")" + netmask + R"(")";
        if (!dns.isEmpty())
            jsonDoc += R"(,"wifiStaticDNS":")" + dns + R"(")";
        jsonDoc += "}";
        _deviceNetConfig.setJsonDoc(jsonDoc.c_str());
        LOG_I(MODULE_PREFIX, "apiWifiIP set %s - used from the next WiFi start", jsonDoc.c_str());
    }
    String settingJSON = R"("wifiip":)" + String(_deviceNetConfig.getJsonDoc());
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, settingJSON.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi diagnostics
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode NetworkManager::apiWifiDiag(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Set any options given
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    if (nameValues.size() > 0)
    {
        RaftJson options = RaftJson::getJSONFromNVPairs(nameValues, true);
        String errStr;
        if (!networkSystem.getWiFiDiagnostics().setOptions(options, errStr))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, errStr.c_str());
    }

    // Return diagnostics
    String diagJSON = "\"diag\":" + networkSystem.getWiFiDiagnostics().getJSON();
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, diagJSON.c_str());
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
        // Configure WiFi (starting it if it is started on demand and not yet started)
        startWiFi(false);
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
        // Configure WiFi AP (starting WiFi if it is started on demand and not yet started)
        startWiFi(false);
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

    // Clear stored credentials back to default (WiFi must be started to do this)
    startWiFi(false);
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
