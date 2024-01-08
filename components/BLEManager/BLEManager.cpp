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
#include "BLEGattOutbound.h"

// Log prefix
static const char *MODULE_PREFIX = "BLEMan";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BLEManager::BLEManager(const char *pModuleName, RaftJsonIF& sysConfig, const char* defaultAdvName)
    : SysModBase(pModuleName, sysConfig)

#ifdef CONFIG_BT_ENABLED

        , _gapServer([this](){
                        return getAdvertisingName();
                    },
                    [this](bool isConnected){
                        executeStatusChangeCBs(isConnected);
                    })
#endif
{
#ifdef CONFIG_BT_ENABLED    
    // BLE interface
    _defaultAdvName = defaultAdvName;
#endif
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
        // Settings
        uint32_t maxPacketLength = configGetLong("maxPktLen", BLEGattOutbound::MAX_BLE_PACKET_LEN_DEFAULT);
        uint32_t outboundQueueSize = configGetLong("outQSize", BLEGattOutbound::DEFAULT_OUTBOUND_MSG_QUEUE_SIZE);

        // Separate task for sending
        bool useTaskForSending = configGetBool("taskEnable", BLEGattOutbound::DEFAULT_USE_TASK_FOR_SENDING);
        uint32_t taskCore = configGetLong("taskCore", BLEGattOutbound::DEFAULT_TASK_CORE);
        int32_t taskPriority = configGetLong("taskPriority", BLEGattOutbound::DEFAULT_TASK_PRIORITY);
        int taskStackSize = configGetLong("taskStack", BLEGattOutbound::DEFAULT_TASK_SIZE_BYTES);

        // Send using indication
        bool sendUsingIndication = configGetBool("sendUseInd", true);

        // Check if advertising interval is specified (0 if not which sets default)
        uint32_t advertisingIntervalMs = configGetLong("advIntervalMs", 0);

        // Get UUIDs for cmd/resp service
        String uuidCmdRespService = configGetString("uuidCmdRespService", "");
        String uuidCmdRespCommand = configGetString("uuidCmdRespCommand", "");
        String uuidCmdRespResponse = configGetString("uuidCmdRespResponse", "");

        // Check for stdServices (such as Battery, Device Info, etc)
        std::vector<String> stdServices;
        configGetArrayElems("stdServices", stdServices);
        bool batteryService = false;
        bool deviceInfoService = false;
        bool heartRate = false;
        for (auto it = stdServices.begin(); it != stdServices.end(); ++it)
        {
            if ((*it).equalsIgnoreCase("battery"))
                batteryService = true;
            else if ((*it).equalsIgnoreCase("devInfo"))
                deviceInfoService = true;
            else if ((*it).equalsIgnoreCase("heartRate"))
                heartRate = true;
        }

        // Setup BLE GAP
        bool isOk = _gapServer.setup(getCommsCore(),
                    maxPacketLength, outboundQueueSize, 
                    useTaskForSending, taskCore, taskPriority, taskStackSize,
                    sendUsingIndication, advertisingIntervalMs,
                    uuidCmdRespService, uuidCmdRespCommand, uuidCmdRespResponse,
                    batteryService, deviceInfoService, heartRate);

        // Log level
        String nimbleLogLevel = configGetString("nimLogLev", "");
        setModuleLogLevel("NimBLE", nimbleLogLevel);

        // Debug
        if (useTaskForSending)
        {
            LOG_I(MODULE_PREFIX, "setup maxPktLen %d task %s core %d priority %d stack %d outQSlots %d minMsBetweenSends %d advIntervalMs %d",
                        maxPacketLength,
                        isOk ? "OK" : "FAILED",
                        taskCore, taskPriority, taskStackSize,
                        outboundQueueSize,
                        BLEGattOutbound::BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS,
                        (int)advertisingIntervalMs);
        }
        else
        {
            LOG_I(MODULE_PREFIX, "setup maxPktLen %d using service loop outQSlots %d minMsBetweenSends %d advIntervalMs %d",
                        maxPacketLength,
                        outboundQueueSize,
                        BLEGattOutbound::BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS,
                        (int)advertisingIntervalMs);
        }
        if (!uuidCmdRespService.isEmpty())
        {
            LOG_I(MODULE_PREFIX, "setup uuidCmdRespService %s uuidCmdRespCommand %s uuidCmdRespResponse %s",
                        uuidCmdRespService.c_str(), uuidCmdRespCommand.c_str(), uuidCmdRespResponse.c_str());
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
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::service()
{
#ifdef CONFIG_BT_ENABLED    
    // Check enabled
    if (!_enableBLE)
        return;

    // Service BLE GAP
    _gapServer.service();
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

String BLEManager::getStatusJSON()
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

String BLEManager::getDebugJSON()
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
        case 'R': { return _gapServer.getRSSI(isValid); }
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
        adName = _defaultAdvName;
    if (adName.length() == 0)
        adName = getSystemName();
    return adName;
}
#endif
