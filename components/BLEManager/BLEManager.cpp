/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEManager
// Handles BLE connectivity and data
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include "BLEManager.h"
#include <RestAPIEndpointManager.h>
#include <SysManager.h>
#include "BLEGattOutbound.h"

// Log prefix
static const char *MODULE_PREFIX = "BLEMan";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BLEManager::BLEManager(const char *pModuleName, ConfigBase &defaultConfig, ConfigBase *pGlobalConfig, 
                ConfigBase *pMutableConfig, const char* defaultAdvName)
    : SysModBase(pModuleName, defaultConfig, pGlobalConfig, pMutableConfig),
        _gapServer([this](){
                        return getAdvertisingName();
                    },
                    [this](bool isConnected){
                        executeStatusChangeCBs(isConnected);
                    })
{
    // BLE interface
    _defaultAdvName = defaultAdvName;
}

BLEManager::~BLEManager()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::setup()
{
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

        // Setup BLE GAP
        bool isOk = _gapServer.setup(getCommsCore(),
                    maxPacketLength, outboundQueueSize, 
                    useTaskForSending, taskCore, taskPriority, taskStackSize,
                    sendUsingIndication);

        // Log level
        String nimbleLogLevel = configGetString("nimLogLev", "");
        setModuleLogLevel("NimBLE", nimbleLogLevel);

        // Debug
        if (useTaskForSending)
        {
            LOG_I(MODULE_PREFIX, "setup maxPktLen %d task %s core %d priority %d stack %d outQSlots %d minMsBetweenSends %d",
                        maxPacketLength,
                        isOk ? "OK" : "FAILED",
                        taskCore, taskPriority, taskStackSize,
                        outboundQueueSize,
                        BLEGattOutbound::BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS);
        }
        else
        {
            LOG_I(MODULE_PREFIX, "setup maxPktLen %d using service loop outQSlots %d minMsBetweenSends %d",
                        maxPacketLength,
                        outboundQueueSize,
                        BLEGattOutbound::BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS);
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
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::service()
{
    // Check enabled
    if (!_enableBLE)
        return;

    // Service BLE GAP
    _gapServer.service();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// REST API Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    endpointManager.addEndpoint("blerestart", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                        std::bind(&BLEManager::apiBLERestart, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        "Restart BLE");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API Restart BLE
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode BLEManager::apiBLERestart(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Restart BLE GAP Server
    _gapServer.restart();

    // Restart in progress
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Comms channels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::addCommsChannels(CommsCoreIF& commsCoreIF)
{
    // Add comms channel
    _gapServer.registerChannel(commsCoreIF);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get status JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String BLEManager::getStatusJSON()
{
    return R"({"rslt":"ok",)" + _gapServer.getStatusJSON(false, false) + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String BLEManager::getDebugJSON()
{
    return _gapServer.getStatusJSON(true, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GetNamedValue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double BLEManager::getNamedValue(const char* valueName, bool& isValid)
{
    switch(valueName[0])
    {
        case 'R': { return _gapServer.getRSSI(isValid); }
        default: { isValid = false; return 0; }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get advertising name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

