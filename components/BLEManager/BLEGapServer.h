/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGapServer
// Handles BLE GAP
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "sdkconfig.h"
#ifdef CONFIG_BT_ENABLED

#include "host/ble_uuid.h"
#endif
#include "BLEManStats.h"
#include "BLEGattServer.h"
#include "CommsCoreIF.h"

#define USE_TIMED_ADVERTISING_CHECK 1

// Callback types
typedef std::function<String()> GetAdvertisingNameFnType;
typedef std::function<void(bool isConnected)> StatusChangeFnType;

class BLEGapServer
{
public:

#ifdef CONFIG_BT_ENABLED

    // Constructor
    BLEGapServer(GetAdvertisingNameFnType getAdvertisingNameFn, 
                StatusChangeFnType statusChangeFn);
    virtual ~BLEGapServer();

    // Setup
    // passing 0 for advertisingIntervalMs will use the default
    bool setup(CommsCoreIF* pCommsCoreIF,
                uint32_t maxPacketLen, 
                uint32_t outboundQueueSize, bool useTaskForSending,
                uint32_t taskCore, int32_t taskPriority, int taskStackSize,
                bool sendUsingIndication,
                uint32_t advertisingIntervalMs = 0,
                const String& uuidCmdRespService = "",
                const String& uuidCmdRespCommand = "",
                const String& uuidCmdRespResponse = "",
                bool batteryService = false,
                bool deviceInfoService = false,
                bool heartRate = false);
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
    static const uint32_t BLE_GAP_MAX_ADV_NAME_LEN = 31;

    // Status change function
    StatusChangeFnType _statusChangeFn = nullptr;
    
    // Addr type - this is discovered using ble_hs_id_infer_auto() and somehow
    // relates to bonding
    uint8_t _ownAddrType = 0;

    // Preferred connection params
    static const uint32_t LL_PACKET_TIME = 2500;
    static const uint32_t LL_PACKET_LENGTH = 251;

    // Preferred connection interval
    static const uint16_t PREF_CONN_INTERVAL = 6; // 7.5ms
    static const uint16_t PREF_CONN_LATENCY = 0;
    static const uint16_t PREF_SUPERVISORY_TIMEOUT = 1000; // 10s

    // Gatt server
    BLEGattServer _gattServer;

    // ChannelID used to identify this message channel to the CommsCoreIF
    uint32_t _commsChannelID = CommsCoreIF::CHANNEL_ID_UNDEFINED;

    // Status
    bool _isConnected = false;
    uint16_t _bleGapConnHandle = 0;

    // RSSI - updated regularly
    int8_t _rssi = 0;
    uint32_t _rssiLastMs = 0;
    static const uint32_t RSSI_CHECK_MS = 2000;

    // Max packet length - seems to be OS dependent (iOS seems to truncate at 182?)
    uint32_t _maxPacketLength = BLEGattOutbound::MAX_BLE_PACKET_LEN_DEFAULT;

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

    // Advertising interval
    bool _useSpecifiedAdvertisingInterval = false;
    uint32_t _advertisingIntervalMs = 1000;

#ifdef USE_TIMED_ADVERTISING_CHECK
    // Advertising check timeout
    bool _advertisingCheckRequired = false;
    uint32_t _advertisingCheckMs = 0;
    static const uint32_t ADVERTISING_CHECK_MS = 3000;
#endif

    // Check connection interval some time after connection
    bool _connIntervalCheckPending = false;
    uint32_t _connIntervalCheckPendingStartMs = 0;
    static const uint32_t CONN_INTERVAL_CHECK_MS = 200;

    // Advertising
    bool startAdvertising();
    void stopAdvertising();
    void serviceTimedAdvertisingCheck();
    
    // Callbacks
    void onSync();
    int nimbleGapEvent(struct ble_gap_event *event);

    // Set connection state
    void setConnState(bool isConnected, uint16_t connHandle = 0);

    // GATT access callback
    void gattAccessCallback(const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength);

    // Service handling
    bool serviceRestartIfRequired();

    // RSSI value
    void serviceGettingRSSI();

    // GAP event helpers
    String getGapEventName(int eventType);
    int gapEventConnect(struct ble_gap_event *event, String& statusStr, int& connHandle);
    int gapEventDisconnect(struct ble_gap_event *event, String& statusStr, int& connHandle);
    int gapEventConnUpdate(struct ble_gap_event *event, String& statusStr, int& connHandle);
    int gapEventRepeatPairing(struct ble_gap_event *event);
    static void debugLogConnInfo(const char* prefix, struct ble_gap_conn_desc *desc);

    // Message sending
    bool isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
    bool sendBLEMsg(CommsChannelMsg& msg);

    // Task
    static void bleHostTask(void *param);
    static void print_addr(const uint8_t *addr);
    bool nimbleStart();
    bool nimbleStop();
    uint32_t parkmiller_next(uint32_t seed) const;
    void requestConnInterval();

#else // CONFIG_BT_ENABLED

public:
    // Constructor
    BLEGapServer(GetAdvertisingNameFnType getAdvertisingNameFn, 
                StatusChangeFnType statusChangeFn) {}

    // Setup
    bool setup(CommsCoreIF* pCommsCoreIF,
                uint32_t maxPacketLen, 
                uint32_t outboundQueueSize, bool useTaskForSending,
                uint32_t taskCore, int32_t taskPriority, int taskStackSize,
                bool sendUsingIndication)
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

    // Restart
    void restart() {}

    // Get max packet len
    void registerChannel(CommsCoreIF& commsCoreIF) {}

    // Get RSSI
    double getRSSI(bool& isValid)
    {
        isValid = false;
        return 0;
    }

#endif // CONFIG_BT_ENABLED

};
