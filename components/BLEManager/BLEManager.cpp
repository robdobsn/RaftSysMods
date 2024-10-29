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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BLEManager::BLEManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)

#ifdef CONFIG_BT_ENABLED

        , _gapServer([this](){
                        return getAdvertisingName();
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
        SysManager* pSysManager = getSysManager();
        if (pSysManager)
        {
            BLEStdServices::systemManufacturer = pSysManager->getSystemManufacturer();
            BLEStdServices::systemModel = pSysManager->getSystemName();
            BLEStdServices::systemSerialNumber = pSysManager->getSystemSerialNo();
            BLEStdServices::firmwareVersionNumber = pSysManager->getSystemVersion();
            BLEStdServices::hardwareRevisionNumber = pSysManager->getBaseSysTypeVersion();
        }

        // Settings
        BLEConfig bleConfig;
        bleConfig.setup(modConfig());

        // Setup BLE GAP
        bool isOk = _gapServer.setup(getCommsCore(), bleConfig);

        // Log level
        String nimbleLogLevel = configGetString("nimLogLev", "");
        setModuleLogLevel("NimBLE", nimbleLogLevel);

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
// REST API Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
#ifdef CONFIG_BT_ENABLED
    endpointManager.addEndpoint("blerestart", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                        std::bind(&BLEManager::apiBLERestart, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        "Restart BLE");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API Restart BLE
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// GetNamedValue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Get advertising name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_BT_ENABLED
String BLEManager::getAdvertisingName()
{
    // Name
    String adName = configGetString("adName", "");
    if (adName.length() == 0)
    {
        bool friendlyNameIsSet = false;
        adName = getFriendlyName(friendlyNameIsSet);
    }
    if (adName.length() == 0)
        adName = getSystemName();
    return adName;
}
#endif
