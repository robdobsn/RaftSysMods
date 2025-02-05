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

// #define DEBUG_BLE_ADVERTISING_DATA

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BLEManager::BLEManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)

#ifdef CONFIG_BT_ENABLED

        , _gapServer([this](std::vector<uint8_t>& manufacturerData, ble_uuid128_t& serviceFilterUUID){
                        getAdvertisingData(manufacturerData, serviceFilterUUID);
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

        // Log level
        String nimbleLogLevel = configGetString("nimLogLev", "");
        setModuleLogLevel("NimBLE", nimbleLogLevel);

        // Setup service UUID used to filter devices
        if (!bleConfig.uuidFilterService.isEmpty())
        {
            Raft::uuid128FromString(bleConfig.uuidFilterService.c_str(), _serviceFilterUUID.value);
            _serviceFilterUUID.u.type = BLE_UUID_TYPE_128;
            _serviceFilterUUIDValid = true;
        }

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get advertising data
/// @param manufacturerData
void BLEManager::getAdvertisingData(std::vector<uint8_t>& manufacturerData, ble_uuid128_t& serviceFilterUUID)
{
    // Get serial number
    String serialNo = getSysManager() ? getSysManager()->getSystemSerialNo() : "";
    if (serialNo.length() == 0)
        return;

    // Convert from hex to binary
    auto serialNoBytes = Raft::getBytesFromHexStr(serialNo.c_str(), MAX_SERIAL_NO_BYTES);

    // Check if manuf ID is specified - if so advertise manufacturer data
    String manufID = configGetString("advManufID", "");
    if (manufID.length() != 0)
    {
        // Extract manufacturer code from config if present
        std::vector<uint8_t> manufIDBytes = Raft::getBytesFromHexStr(manufID.c_str(), MAX_MANUFACTURER_ID_LEN);
        manufacturerData.assign(manufIDBytes.begin(), manufIDBytes.end());

        // Add any fixed data specified in config
        std::vector<uint8_t> fixedData = Raft::getBytesFromHexStr(configGetString("advManufData", "").c_str(), MAX_MANUFACTURER_DATA_LEN);
        manufacturerData.insert(manufacturerData.end(), fixedData.begin(), fixedData.end());

        // Check if we need to limit any further data
        int advManufValueBytes = configGetLong("advManufValueBytes", -1);
        
        // Check if any additional data is specified from named values
        String advManufValue = configGetString("advManufValue", "");
        if (advManufValue.equalsIgnoreCase("serialNo"))
        {
            // Serial number
            if (serialNoBytes.size() > 0)
            {
                // Use the last N digits of the serial number
                uint32_t startPos = serialNoBytes.size() > advManufValueBytes ? serialNoBytes.size() - advManufValueBytes : 0;
                manufacturerData.insert(manufacturerData.end(), serialNoBytes.begin() + startPos, serialNoBytes.begin() + serialNoBytes.size());
            }
        }
    }

    // Check if service filter UUID is specified
    if (_serviceFilterUUIDValid)
    {
        serviceFilterUUID = generateServiceFilterUUID(serialNoBytes);
    }

#ifdef DEBUG_BLE_ADVERTISING_DATA
    LOG_I(MODULE_PREFIX, "getAdvertisingData serialNo %s serviceFilterValid %d serviceFilterUUIDType %d serviceFilterUUID %s", 
                serialNo.c_str(), _serviceFilterUUIDValid, serviceFilterUUID.u.type, Raft::uuid128ToString(serviceFilterUUID.value).c_str());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate UUID for service filtering based on device serial number
/// @param serialNoBytes Serial number in byte format (if formatted as hex this would be 2 digits per byte)
/// @param serviceFilterUUID UUID to receive the service filter
ble_uuid128_t BLEManager::generateServiceFilterUUID(const std::vector<uint8_t>& serialNoBytes)
{
    // Generate a UUID based on the serial number
    const uint32_t UUID_128_BYTES = 16;
    const uint32_t bytesToProc = serialNoBytes.size() < UUID_128_BYTES ? serialNoBytes.size() : UUID_128_BYTES;
    ble_uuid128_t modifiedUUID = _serviceFilterUUID;
    for (int i = 0; i < bytesToProc; i++)
    {
        modifiedUUID.value[i] ^= serialNoBytes[bytesToProc - 1 - i];
    }
    return modifiedUUID;
}
