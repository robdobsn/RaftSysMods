/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEManager
// Handles BLE connectivity and data
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "BLEManager.h"
#include "RestAPIEndpointManager.h"
#include "SysManager.h"
#include "BLEConfig.h"
#include "BLEStdServices.h"
#include "sdkconfig.h"

// #define DEBUG_BLE_ADVERTISING_DATA

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BLEManager::BLEManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)

#ifdef CONFIG_BT_ENABLED

        , _gapServer([this](String& advName, uint16_t& manufacturerData, String& serialNo){
                        getAdvertisingInfo(advName, manufacturerData, serialNo);
                    },
                    [this](bool isConnected){
                        executeStatusChangeCBs(isConnected);
                    })
#endif
{
}

BLEManager::~BLEManager()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::setup()
{
#ifdef CONFIG_BT_ENABLED
    // See if BLE enabled
    _enableBLE = configGetBool("enable", false);

    // Setup if enabled
    if (_enableBLE)
    {
        // Set system information (must not be changed after setup)
        SysManagerIF* pSysManager = getSysManager();
        if (pSysManager)
        {
            bool isValid = false;
            BLEStdServices::systemManufacturer = pSysManager->getNamedString(nullptr, "Manufacturer", isValid);
            BLEStdServices::systemModel = pSysManager->getNamedString(nullptr, "SystemName", isValid);
            BLEStdServices::systemSerialNumber = pSysManager->getNamedString(nullptr, "SerialNumber", isValid);
            BLEStdServices::firmwareVersionNumber = pSysManager->getNamedString(nullptr, "SystemVersion", isValid);
            BLEStdServices::hardwareRevisionNumber = pSysManager->getNamedString(nullptr, "BaseSysTypeVersion", isValid);
        }

        // Settings
        BLEConfig bleConfig;
        bleConfig.setup(modConfig());

        // Log level
        String nimbleLogLevel = configGetString("nimLogLev", "");
        setModuleLogLevel("NimBLE", nimbleLogLevel);

        // Setup BLE GAP
        bool isOk = _gapServer.setup(getCommsCore(), bleConfig);

        // Debug
        if (bleConfig.useTaskForSending)
        {
            LOG_I(MODULE_PREFIX, "setup maxPktLen %d task %s core %d priority %d stack %d outQSlots %d minMsBetweenSends %d advIntervalMs %d",
                        bleConfig.maxPacketLen,
                        isOk ? "OK" : "FAILED",
                        bleConfig.taskCore, bleConfig.taskPriority, bleConfig.taskStackSize,
                        bleConfig.outboundQueueSize,
                        bleConfig.minMsBetweenSends,
                        (int)bleConfig.advertisingIntervalMs);
        }
        else
        {
            LOG_I(MODULE_PREFIX, "setup maxPktLen %d using service loop outQSlots %d minMsBetweenSends %d advIntervalMs %d",
                        bleConfig.maxPacketLen,
                        bleConfig.outboundQueueSize,
                        bleConfig.minMsBetweenSends,
                        (int)bleConfig.advertisingIntervalMs);
        }
        if (!bleConfig.uuidCmdRespService.isEmpty())
        {
            LOG_I(MODULE_PREFIX, "setup uuidCmdRespService %s uuidCmdRespCommand %s uuidCmdRespResponse %s",
                        bleConfig.uuidCmdRespService.c_str(), bleConfig.uuidCmdRespCommand.c_str(), bleConfig.uuidCmdRespResponse.c_str());
        }
        if (!bleConfig.uuidFilterService.isEmpty())
        {
            LOG_I(MODULE_PREFIX, "setup uuidFilterService %s", bleConfig.uuidFilterService.c_str());
        }
    }
    else
    {
        // Teardown BLE
        _gapServer.teardown();

        // Debug
        if (_enableBLE)
        {
            LOG_I(MODULE_PREFIX, "setup deinit ok");
        }
    }
#else
    LOG_E(MODULE_PREFIX, "setup BLE is not enabled in sdkconfig");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop (called frequently)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::loop()
{
#ifdef CONFIG_BT_ENABLED    
    // Check enabled
    if (!_enableBLE)
        return;

    // Service BLE GAP
    _gapServer.loop(getSysManager());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief REST API Endpoints
/// @param endpointManager
void BLEManager::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
#ifdef CONFIG_BT_ENABLED
    endpointManager.addEndpoint("blerestart", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                        std::bind(&BLEManager::apiBLERestart, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        "Restart BLE");
    endpointManager.addEndpoint("bledisconnect", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                        std::bind(&BLEManager::apiBLEDisconnect, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        "Disconnect BLE");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API Restart BLE
/// @param reqStr request string
/// @param respStr response string (out) JSON response
/// @param sourceInfo source of the API call
/// @return RaftRetCode
#ifdef CONFIG_BT_ENABLED
RaftRetCode BLEManager::apiBLERestart(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Restart BLE GAP Server
    _gapServer.restart();

    // Restart in progress
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API Disconnect BLE
/// @param reqStr request string
/// @param respStr response string (out) JSON response
/// @param sourceInfo source of the API call
/// @return RaftRetCode
#ifdef CONFIG_BT_ENABLED
RaftRetCode BLEManager::apiBLEDisconnect(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Request disconnect of BLE GAP Server after time interval - to allow response to be sent
    _gapServer.requestTimedDisconnect();

    // Disconnect in progress
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Comms channels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::addCommsChannels(CommsCoreIF& commsCoreIF)
{
#ifdef CONFIG_BT_ENABLED 
    // Add comms channel
    _gapServer.registerChannel(commsCoreIF);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get status JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String BLEManager::getStatusJSON() const
{
#ifdef CONFIG_BT_ENABLED    
    return R"({"rslt":"ok",)" + _gapServer.getStatusJSON(false, false) + "}";
#else
    return R"({"rslt":"failNoBLE"})";
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String BLEManager::getDebugJSON() const
{
#ifdef CONFIG_BT_ENABLED
    return _gapServer.getStatusJSON(true, true);
#else
    return R"({"rslt":"failNoBLE"})";
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a named value from the module
/// @param valueName 
/// @param isValid 
/// @return value
double BLEManager::getNamedValue(const char* valueName, bool& isValid)
{
    switch(valueName[0])
    {
#ifdef CONFIG_BT_ENABLED
        case 'R': 
        case 'r': { return _gapServer.getRSSI(isValid); }
        case 'C':
        case 'c': { isValid = true; return _gapServer.isConnected(); }
#endif
        default: { isValid = false; return 0; }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set a named value in the module
/// @param valueName
/// @param value
/// @return true if set
bool BLEManager::setNamedValue(const char* valueName, double value)
{
    if (strcasecmp(valueName, "connintvms") == 0)
    {
#ifdef CONFIG_BT_ENABLED
        _gapServer.setReqConnInterval(value);
#endif
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get advertising name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_BT_ENABLED

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get advertising info
/// @param advName
/// @param manufacturerData
/// @param serialNo
void BLEManager::getAdvertisingInfo(String& advName, uint16_t& manufacturerID, String& serialNo)
{
    // Advertising name
    advName = configGetString("adName", "");
    if (advName.length() == 0)
    {
        bool friendlyNameIsSet = false;
        advName = getFriendlyName(friendlyNameIsSet);
    }
    if (advName.length() == 0)
        advName = getSystemName();
     
    // Get serial number
    bool isValid = false;
    serialNo = getSysManager() ? getSysManager()->getNamedString(nullptr, "SerialNumber", isValid) : "";
    if (serialNo.length() == 0)
        return;

    // Get manufacturer ID
    manufacturerID = configGetLong("advManufID", 0x004c);
}

#endif
