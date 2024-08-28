/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEConfig.h
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "sdkconfig.h"

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
