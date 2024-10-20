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
#include <stdint.h>
#include <vector>

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
    static const uint32_t DEFAULT_CONN_INTERVAL_MS = 15;
    static const uint32_t DEFAULT_CONN_LATENCY = 0;
    static const uint32_t PREF_SUPERVISORY_TIMEOUT_MS = 10000;
    static const uint32_t DEFAULT_LL_PACKET_TIME = 2500;
    static const uint32_t DEFAULT_LL_PACKET_LENGTH = 251;

    bool setup(const RaftJsonIF& config)
    {
        // Enables for peripheral and central roles
        enPeripheral = config.getBool("peripheral", true);
        enCentral = config.getBool("central", false);

        // Set up BLE parameters from configuration
        maxPacketLen = config.getLong("maxPktLen", MAX_BLE_PACKET_LEN_DEFAULT);
        preferredMTUSize = config.getLong("mtuSize", PREFERRED_MTU_SIZE);
        outboundQueueSize = config.getLong("outQSize", DEFAULT_OUTBOUND_MSG_QUEUE_SIZE);
        useTaskForSending = config.getBool("taskEnable", DEFAULT_USE_TASK_FOR_SENDING);
        taskCore = config.getLong("taskCore", DEFAULT_TASK_CORE);
        taskPriority = config.getLong("taskPriority", DEFAULT_TASK_PRIORITY);
        taskStackSize = config.getLong("taskStack", DEFAULT_TASK_SIZE_BYTES);
        sendUsingIndication = config.getBool("sendUseInd", true);
        minMsBetweenSends = config.getLong("minMsBetweenSends", BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS);
        outMsgsInFlightMax = config.getLong("outMsgsInFlightMax", DEFAULT_NUM_OUTBOUND_MSGS_IN_FLIGHT_MAX);
        outMsgsInFlightTimeoutMs = config.getLong("outMsgsInFlightMs", BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS);
        supvTimeoutPrefMs = config.getLong("supvTimeoutPrefMs", PREF_SUPERVISORY_TIMEOUT_MS);
        llPacketTimePref = config.getLong("llPacketTimePref", DEFAULT_LL_PACKET_TIME);
        llPacketLengthPref = config.getLong("llPacketLengthPref", DEFAULT_LL_PACKET_LENGTH);
        advertisingIntervalMs = config.getLong("advIntervalMs", 0);
        connIntervalPreferredMs = config.getLong("connIntvPrefMs", DEFAULT_CONN_INTERVAL_MS);
        connLatencyPref = config.getLong("connLatencyPref", DEFAULT_CONN_LATENCY);

        // Get UUIDs for cmd/resp service
        uuidCmdRespService = config.getString("uuidCmdRespService", "");
        uuidCmdRespCommand = config.getString("uuidCmdRespCommand", "");
        uuidCmdRespResponse = config.getString("uuidCmdRespResponse", "");

        // Standard services (battery, device info, etc.)
        std::vector<String> stdServices;
        config.getArrayElems("stdServices", stdServices);
        batteryService = false;
        deviceInfoService = false;
        heartRateService = false;
        for (const auto& service : stdServices)
        {
            if (service.equalsIgnoreCase("battery"))
                batteryService = true;
            else if (service.equalsIgnoreCase("devInfo"))
                deviceInfoService = true;
            else if (service.equalsIgnoreCase("heartRate"))
                heartRateService = true;
        }

        return true;
    }

    String debugStr() const
    {
        // Prepare a debug string with all configuration parameters
        String str = "BLEConfig: enPer:" + String(enPeripheral) + 
                     " enCen:" + String(enCentral) + 
                     " maxPktLn:" + String(maxPacketLen) + 
                     " mtuSz:" + String(preferredMTUSize) +
                     " outQSz:" + String(outboundQueueSize) +
                     " tskEn:" + String(useTaskForSending) + 
                     " tskCore:" + String(taskCore) + 
                     " tskPrty:" + String(taskPriority) + 
                     " tskStk:" + String(taskStackSize) +
                     " useInd:" + String(sendUsingIndication) + 
                     " minSndMs:" + String(minMsBetweenSends) + 
                     " inFlghtMax:" + String(outMsgsInFlightMax) +
                     " inFlghtMs:" + String(outMsgsInFlightTimeoutMs) +
                     " supvTOMs:" + String(supvTimeoutPrefMs) +
                     " llPktTPref:" + String(llPacketTimePref) + 
                     " llPktLPref:" + String(llPacketLengthPref) + 
                     " advMs:" + String(advertisingIntervalMs) + 
                     " conPrefMs:" + String(connIntervalPreferredMs) + 
                     " conLatPref:" + String(connLatencyPref) + 
                     " uuidCmdRspSvc:" + uuidCmdRespService +
                     " uuidCmdRspCmd:" + uuidCmdRespCommand +
                     " uuidCmdRspResp:" + uuidCmdRespResponse +
                     " battSvc:" + String(batteryService) +
                     " devInfSvc:" + String(deviceInfoService) + 
                     " hrmSvc:" + String(heartRateService);
        return str;
    }

    // Config settings
    bool enPeripheral = true;
    bool enCentral = false;
    uint32_t maxPacketLen = MAX_BLE_PACKET_LEN_DEFAULT;
    uint32_t preferredMTUSize = PREFERRED_MTU_SIZE;
    uint32_t outboundQueueSize = DEFAULT_OUTBOUND_MSG_QUEUE_SIZE;
    bool useTaskForSending = DEFAULT_USE_TASK_FOR_SENDING;
    uint32_t taskCore = DEFAULT_TASK_CORE;
    int32_t taskPriority = DEFAULT_TASK_PRIORITY;
    int taskStackSize = DEFAULT_TASK_SIZE_BYTES;
    bool sendUsingIndication = true;
    uint32_t outMsgsInFlightMax = DEFAULT_NUM_OUTBOUND_MSGS_IN_FLIGHT_MAX;
    uint32_t outMsgsInFlightTimeoutMs = BLE_OUTBOUND_MSGS_IN_FLIGHT_TIMEOUT_MS;
    uint32_t minMsBetweenSends = BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS;
    uint32_t advertisingIntervalMs = 0;
    uint32_t connIntervalPreferredMs = DEFAULT_CONN_INTERVAL_MS;
    uint32_t connLatencyPref = DEFAULT_CONN_LATENCY;
    uint32_t supvTimeoutPrefMs = PREF_SUPERVISORY_TIMEOUT_MS;
    uint32_t llPacketTimePref = DEFAULT_LL_PACKET_TIME;
    uint32_t llPacketLengthPref = DEFAULT_LL_PACKET_LENGTH;
    String uuidCmdRespService;
    String uuidCmdRespCommand;
    String uuidCmdRespResponse;
    bool batteryService = false;
    bool deviceInfoService = false;
    bool heartRateService = false;
};
