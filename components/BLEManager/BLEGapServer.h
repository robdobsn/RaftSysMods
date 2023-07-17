/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGapServer
// Handles BLE GAP
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <sdkconfig.h>
#ifdef CONFIG_BT_ENABLED
#include "host/ble_uuid.h"
#endif
#include <ThreadSafeQueue.h>
#include <ProtocolRawMsg.h>
#include "BLEManStats.h"
#include "BLEGattServer.h"
#include <CommsCoreIF.h>

#define USE_TIMED_ADVERTISING_CHECK 1

// Callback types
typedef std::function<String()> GetAdvertisingNameFnType;
typedef std::function<void(bool isConnected)> StatusChangeFnType;

class BLEGapServer
{
public:

    // Defaults
    static const int DEFAULT_OUTBOUND_MSG_QUEUE_SIZE = 30;
    static const uint32_t MAX_BLE_PACKET_LEN_DEFAULT = 450;
    static const bool DEFAULT_USE_TASK_FOR_SENDING = false;
    static const int DEFAULT_TASK_CORE = 0;
    static const int DEFAULT_TASK_PRIORITY = 1;
    static const int DEFAULT_TASK_SIZE_BYTES = 4000;
    static const uint32_t BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS = 25;

#ifdef CONFIG_BT_ENABLED

    // Constructor
    BLEGapServer();
    virtual ~BLEGapServer();

    // Setup
    bool setup(CommsCoreIF* pCommsCoreIF,
                GetAdvertisingNameFnType getAdvertisingNameFn, 
                StatusChangeFnType statusChangeFn,
                uint32_t maxPacketLen, 
                uint32_t outboundQueueSize, bool useTaskForSending,
                UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize);
    void teardown();

    // Service
    void service();

    // Get status JSON
    String getStatusJSON(bool includeBraces, bool shortForm);

    // Restart
    void restart();

    // Get max packet len
    void registerChannel(CommsCoreIF& commsCoreIF);

    // Get RSSI
    double getRSSI(bool& isValid);

private:
    // Singleton
    static BLEGapServer* _pThis;

    // Comms core interface
    CommsCoreIF* _pCommsCoreIF = nullptr;

    // BLE device initialised
    bool _isInit = false;

    // Get advertising name function
    GetAdvertisingNameFnType _getAdvertisingNameFn = nullptr;

    // Status change function
    StatusChangeFnType _statusChangeFn = nullptr;
    
    // Addr type
    uint8_t own_addr_type = 0;

    // Preferred connection params
    static const uint32_t PREFERRED_MTU_VALUE = 512;
    static const uint32_t LL_PACKET_TIME = 2120;
    static const uint32_t LL_PACKET_LENGTH = 251;

    // Gatt server
    BLEGattServer _gattServer;

    // ChannelID used to identify this message channel to the CommsChannelManager object
    uint32_t _commsChannelID = CommsCoreIF::CHANNEL_ID_UNDEFINED;

    // Status
    bool _isConnected = false;
    uint16_t _bleGapConnHandle = 0;
    int8_t _rssi = 0;
    uint32_t _rssiLastMs = 0;

    // RSSI check interval
    static const uint32_t RSSI_CHECK_MS = 2000;

    // Max packet length - seems to be OS dependent (iOS seems to truncate at 182?)
    uint32_t _maxPacketLength = MAX_BLE_PACKET_LEN_DEFAULT;
    
    // Outbound queue for fragments of messages
    ThreadSafeQueue<ProtocolRawMsg> _bleFragmentQueue;

    // Min time between adjacent outbound messages
    uint32_t _lastOutboundMsgMs = 0;

    // Task that runs the outbound queue - if selected by #define in cpp file
    volatile TaskHandle_t _outboundMsgTaskHandle = nullptr;

    // Outbound message in flight
    volatile bool _outboundMsgInFlight = false;
    uint32_t _outbountMsgInFlightStartMs = 0;
    static const uint32_t BLE_OUTBOUND_MSG_IN_FLIGHT_TIMEOUT_MS = 1000;

    // Stats
    BLEManStats _bleStats;

    // BLE performance testing
    static const uint32_t TEST_THROUGHPUT_MAX_PAYLOAD = 500;
    uint32_t _testPerfPrbsState = 1;
    uint32_t _lastTestMsgCount = 0;

    // BLE restart state
    enum BLERestartState
    {
        BLERestartState_Idle,
        BLERestartState_StopRequired,
        BLERestartState_StartRequired
    };
    BLERestartState _bleRestartState = BLERestartState_Idle;
    static const uint32_t BLE_RESTART_BEFORE_STOP_MS = 200;
    static const uint32_t BLE_RESTART_BEFORE_START_MS = 200;
    uint32_t _bleRestartLastMs = 0;

#ifdef USE_TIMED_ADVERTISING_CHECK
    // Advertising check timeout
    bool _advertisingCheckRequired = false;
    uint32_t _advertisingCheckMs = 0;
    static const uint32_t ADVERTISING_CHECK_MS = 3000;
#endif

    // Advertising
    bool startAdvertising();
    void stopAdvertising();    
    
    // Callbacks
    void onSync();
    int nimbleGapEvent(struct ble_gap_event *event);

    // Set connection state
    void setConnState(bool isConnected, uint16_t connHandle = 0);

    // GATT access callback
    void gattAccessCallback(const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength);

    // Task
    static void bleHostTask(void *param);
    static void logConnectionInfo(struct ble_gap_conn_desc *desc);
    static void print_addr(const uint8_t *addr);
    bool isReadyToSend(uint32_t channelID, bool& noConn);
    bool sendBLEMsg(CommsChannelMsg& msg);
    void handleSendFromOutboundQueue();
    // static void outboundMsgTaskStatic(void* pvParameters);
    void outboundMsgTask();
    bool nimbleStart();
    bool nimbleStop();
    uint32_t parkmiller_next(uint32_t seed) const;

#else // CONFIG_BT_ENABLED

public:
    // Constructor
    BLEGapServer() {}

    // Setup
    bool setup(uint32_t maxPacketLen, uint32_t outboundQueueSize, bool useTaskForSending,
                UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize)
    {
        return false;
    }
    void teardown() {}

    // Service
    void service() {}

    // Get status JSON
    String getStatusJSON(bool includeBraces, bool shortForm)
    {
        return R"({"rslt":"failDisabled"})";
    }

#endif // CONFIG_BT_ENABLED

};
