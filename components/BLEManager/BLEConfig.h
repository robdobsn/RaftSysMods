/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEConfig.h
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "sdkconfig.h"
#include "RaftJsonIF.h"
#include "RaftJson.h"
#include "RaftUtils.h"
#include <stdint.h>
#include <cmath>
#include <vector>

// #define DEBUG_BLE_SERVICE_CONFIG

class BLEStandardServiceConfig
{
public:
    void setup(const RaftJsonIF& config)
    {
        // Get service name
        name = config.getString("name", "");

        // Service settings
        serviceSettings = config.getString("settings", "{}");

        // Get properties
        enable = config.getBool("enable", true);
        notify = config.getBool("notify", false);
        indicate = config.getBool("indicate", false);
        read = config.getBool("read", true);
        write = config.getBool("write", false);

        // Timing
        updateIntervalMs = config.getLong("updateIntervalMs", 1000);
    }

    bool enable:1 = false;
    bool notify:1 = false;
    bool indicate:1 = false;
    bool read:1 = false;
    bool write:1 = false;
    String name;
    String serviceSettings;
    uint32_t updateIntervalMs = 0;
};

class BLEConfig
{
public:
    static const bool DEFAULT_USE_TASK_FOR_SENDING = false;
    static const int DEFAULT_TASK_CORE = 0;
    static const int DEFAULT_TASK_PRIORITY = 1;
    static const int DEFAULT_TASK_SIZE_BYTES = 4000;
    static const int DEFAULT_OUTBOUND_MSG_QUEUE_SIZE = 30;
    static const uint32_t BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS = 50;
    static const uint32_t MAX_BLE_PACKET_LEN_DEFAULT = 500;
    static const uint32_t PREFERRED_MTU_SIZE = 512;
    static const uint32_t DEFAULT_NUM_OUTBOUND_MSGS_IN_FLIGHT_MAX = 10;
    static const uint32_t BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS = 500;
    static const uint32_t DEFAULT_CONN_INTERVAL_BLE_UNITS = 12; // 15ms
    static const uint32_t DEFAULT_CONN_LATENCY = 0;
    static const uint32_t PREF_SUPERVISORY_TIMEOUT_MS = 10000;
    static const uint32_t DEFAULT_LL_PACKET_TIME = 2500;
    static const uint32_t DEFAULT_LL_PACKET_LENGTH = 251;
    static const uint32_t DEFAULT_SCAN_INTERVAL_MS = 200;
    static const uint32_t DEFAULT_SCAN_WINDOW_MS = 150;

    bool setup(const RaftJsonIF& config)
    {
        // Enables for peripheral and central roles
        enPeripheral = config.getBool("peripheral", true);
        enCentral = config.getBool("central", false);

        // Scanning
        scanPassive = config.getBool("scanPassive", false);
        scanNoDuplicates = config.getBool("scanNoDup", false);
        scanLimited = config.getBool("scanLimited", false);

        // Connection params
        maxPacketLen = config.getLong("maxPktLen", MAX_BLE_PACKET_LEN_DEFAULT);
        preferredMTUSize = config.getLong("mtuSize", PREFERRED_MTU_SIZE);
        sendUsingIndication = config.getBool("sendUseInd", true);
        supvTimeoutPrefMs = config.getLong("supvTimeoutPrefMs", PREF_SUPERVISORY_TIMEOUT_MS);
        llPacketTimePref = config.getLong("llPacketTimePref", DEFAULT_LL_PACKET_TIME);
        llPacketLengthPref = config.getLong("llPacketLengthPref", DEFAULT_LL_PACKET_LENGTH);        
        double connIntvPrefMs = config.getDouble("connIntvPrefMs", DEFAULT_CONN_INTERVAL_BLE_UNITS * 1.25);
        connIntvPrefMs = Raft::clamp(connIntvPrefMs, 7.5, 4000.0);
        connIntervalPreferredBLEUnits = std::round(connIntvPrefMs / 1.25);
        connLatencyPref = config.getLong("connLatencyPref", DEFAULT_CONN_LATENCY);

        // Advertising
        advertisingIntervalMs = config.getLong("advIntervalMs", 0);
        advManufData = config.getString("advManufData", "");
        advManufTotalByteLimit = config.getLong("advManufValueBytes", 0);
        advManufValue = config.getString("advManufValue", "");

        // Scanning
        scanningIntervalMs = config.getLong("scanIntervalMs", DEFAULT_SCAN_INTERVAL_MS);
        scanningWindowMs = config.getLong("scanWindowMs", DEFAULT_SCAN_WINDOW_MS);
        scanForSecs = config.getLong("scanForSecs", 0);
        scanBTHome = config.getBool("scanBTHome", false);

        // Pairing parameters
        // This corresponds to the BLE_SM_IO_CAP_XXX values
        // See host/ble_sm.h
        pairingSMIOCap = config.getLong("pairIO", 3);
        pairingSecureConn = config.getBool("pairSecureConn", false);

        // Bus connection name - for central role to dissemninate data
        busConnName = config.getString("busConnName", "");

        // Get UUIDs for cmd/resp service
        uuidCmdRespService = config.getString("uuidCmdRespService", "");
        uuidCmdRespCommand = config.getString("uuidCmdRespCommand", "");
        uuidCmdRespResponse = config.getString("uuidCmdRespResponse", "");
        uuidFilterService = config.getString("uuidFilterService", "");
        uuidFilterMaskChars = config.getLong("uuidFilterMaskChars", 16);

        // Outbound message settings
        minMsBetweenSends = config.getLong("minMsBetweenSends", BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS);
        outboundQueueSize = config.getLong("outQSize", DEFAULT_OUTBOUND_MSG_QUEUE_SIZE);
        outMsgsInFlightMax = config.getLong("outMsgsInFlightMax", DEFAULT_NUM_OUTBOUND_MSGS_IN_FLIGHT_MAX);
        outMsgsInFlightTimeoutMs = config.getLong("outMsgsInFlightMs", BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS);

        // Task settings
        taskCore = config.getLong("taskCore", DEFAULT_TASK_CORE);
        taskPriority = config.getLong("taskPriority", DEFAULT_TASK_PRIORITY);
        taskStackSize = config.getLong("taskStack", DEFAULT_TASK_SIZE_BYTES);
        useTaskForSending = config.getBool("taskEnable", DEFAULT_USE_TASK_FOR_SENDING);

        // Standard services (battery, device info, etc.)
        std::vector<String> stdServiceConfigs;
        config.getArrayElems("stdServices", stdServiceConfigs);
        for (const auto& stdServiceConfig : stdServiceConfigs)
        {
            BLEStandardServiceConfig stdServiceCfg;
            stdServiceCfg.setup(RaftJson(stdServiceConfig));
            stdServices.push_back(stdServiceCfg);
        }
        return true;
    }

#ifdef DEBUG_BLE_SERVICE_CONFIG
    String debugStr() const
    {
        // Prepare a debug string with all configuration parameters
        String str = "BLEConfig: enPer:" + String(enPeripheral) + 
                    " enCen:" + String(enCentral) + 
                    " advMs:" + String(advertisingIntervalMs) + 
                    " scanIntMs:" + String(scanningIntervalMs) +
                    " scanWinMs:" + String(scanningWindowMs) +
                    " scanSecs:" + String(scanForSecs) +
                    " scanLim:" + String(scanLimited) +
                    " scanNoDup:" + String(scanNoDuplicates) +
                    " scanPass:" + String(scanPassive) +
                    " scanBTHome:" + String(scanBTHome) +
                    " pairIO:" + String(pairingSMIOCap) +
                    " pairSecConn:" + String(pairingSecureConn) +
                    " useInd:" + String(sendUsingIndication) + 
                    " conItvPrefMs:" + String(connIntervalPreferredBLEUnits*1.25) + 
                    " conLatPref:" + String(connLatencyPref) + 
                    " maxPktLn:" + String(maxPacketLen) + 
                    " MTU:" + String(preferredMTUSize) +
                    " llPktTPref:" + String(llPacketTimePref) + 
                    " llPktLPref:" + String(llPacketLengthPref) + 
                    " supvTOMs:" + String(supvTimeoutPrefMs) +
                    " busConnName:\"" + busConnName + String("\"") +
                    " uuidCmdRspSvc:" + uuidCmdRespService +
                    " uuidCmdRspCmd:" + uuidCmdRespCommand +
                    " uuidCmdRspResp:" + uuidCmdRespResponse +
                    " uuidFilterService " + uuidFilterService +
                    " outQSz:" + String(outboundQueueSize) +
                    " minSndMs:" + String(minMsBetweenSends) + 
                    " inFlghtMax:" + String(outMsgsInFlightMax) +
                    " inFlghtMs:" + String(outMsgsInFlightTimeoutMs) +
                    " tskEn:" + String(useTaskForSending) + 
                    " tskCore:" + String(taskCore) + 
                    " tskPrty:" + String(taskPriority) + 
                    " tskStk:" + String(taskStackSize);

        return str;
    }
#endif

    // Get connection interval preferred in BLE units
    uint16_t getConnIntervalPrefBLEUnits() const
    {
        if (connIntervalPreferredBLEUnits == 0)
            return BLEConfig::DEFAULT_CONN_INTERVAL_BLE_UNITS;
        return connIntervalPreferredBLEUnits;
    }

    // Role
    bool enPeripheral:1 = true;
    bool enCentral:1 = false;

    // Task settings
    bool useTaskForSending:1 = DEFAULT_USE_TASK_FOR_SENDING;

    // Send using indication (instead of notification)
    // Note: indication requires an ACK from the central device
    bool sendUsingIndication:1 = true;

    // Scanning
    bool scanPassive:1 = false;
    bool scanNoDuplicates:1 = false;
    bool scanLimited:1 = false;    
    bool scanBTHome:1 = false;

    // Standard services
    std::vector<BLEStandardServiceConfig> stdServices;

    // Pairing parameters
    bool pairingSecureConn:1 = false;
    uint8_t pairingSMIOCap = 3;

    // Connection params
    uint16_t maxPacketLen = MAX_BLE_PACKET_LEN_DEFAULT;
    uint16_t preferredMTUSize = PREFERRED_MTU_SIZE;
    uint16_t connIntervalPreferredBLEUnits = DEFAULT_CONN_INTERVAL_BLE_UNITS;
    uint16_t connLatencyPref = DEFAULT_CONN_LATENCY;
    uint16_t supvTimeoutPrefMs = PREF_SUPERVISORY_TIMEOUT_MS;
    uint16_t llPacketTimePref = DEFAULT_LL_PACKET_TIME;
    uint16_t llPacketLengthPref = DEFAULT_LL_PACKET_LENGTH;

    // Advertising
    uint16_t advertisingIntervalMs = 0;
    String advManufData;
    uint16_t advManufTotalByteLimit = 0;
    String advManufValue;

    // Scanning
    uint16_t scanningIntervalMs = DEFAULT_SCAN_INTERVAL_MS;
    uint16_t scanningWindowMs = DEFAULT_SCAN_WINDOW_MS;
    int32_t scanForSecs = 0;

    // Bus connection name
    String busConnName;

    // UUIDs
    String uuidCmdRespService;
    String uuidCmdRespCommand;
    String uuidCmdRespResponse;
    String uuidFilterService;

    // UUID filter mask characters
    uint16_t uuidFilterMaskChars = 16;

    // Outbound message settings
    uint16_t minMsBetweenSends = BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS;
    uint16_t outboundQueueSize = DEFAULT_OUTBOUND_MSG_QUEUE_SIZE;
    uint16_t outMsgsInFlightMax = DEFAULT_NUM_OUTBOUND_MSGS_IN_FLIGHT_MAX;
    uint32_t outMsgsInFlightTimeoutMs = BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS;

    // Task settings
    uint8_t taskCore = DEFAULT_TASK_CORE;
    int8_t taskPriority = DEFAULT_TASK_PRIORITY;
    uint16_t taskStackSize = DEFAULT_TASK_SIZE_BYTES;
};
