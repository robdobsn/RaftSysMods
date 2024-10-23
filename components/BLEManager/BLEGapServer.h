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
#undef min
#undef max
#include "host/ble_uuid.h"
#undef min
#undef max
#endif
#include "BLEConsts.h"
#include "BLEManStats.h"
#include "BLEGattServer.h"
#include "CommsCoreIF.h"
#include "BLEConfig.h"
#include "BLEAdvertDecoder.h"

#define USE_TIMED_ADVERTISING_CHECK 1

// Callback types
typedef std::function<String()> GetAdvertisingNameFnType;
typedef std::function<void(bool isConnected)> StatusChangeFnType;

class BLEGapServer
{
public:

#ifdef CONFIG_BT_ENABLED

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Constructor for BLEGapServer
    /// @param getAdvertisingNameFn function pointer to get the advertising name
    /// @param statusChangeFn function pointer to handle status changes
    BLEGapServer(GetAdvertisingNameFnType getAdvertisingNameFn, 
                StatusChangeFnType statusChangeFn);
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destructor for BLEGapServer
    virtual ~BLEGapServer();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Setup BLEGapServer
    /// @param pCommsCoreIF pointer to the CommsCore interface (part of Raft libraries)
    /// @param bleConfig configuration parameters for BLE
    /// @return true if setup was successful
    bool setup(CommsCoreIF* pCommsCoreIF, const BLEConfig& bleConfig);
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Tears down the BLEGapServer, stopping advertising and deinitializing NimBLE
    void teardown();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Services the BLEGapServer by handling advertising, GATT server, and RSSI polling
    void loop();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the status of the BLEGapServer as a JSON string
    /// @param includeBraces if true, the JSON string will be enclosed in braces
    /// @param shortForm if true, the JSON string will be in a short form
    /// @return The status of the BLEGapServer as a JSON string
    String getStatusJSON(bool includeBraces, bool shortForm) const;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Restart the BLEGapServer (by stopping and restarting the BLE stack)
    void restart();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register BLEGapServer as a communication channel with the Raft CommsCore interface
    /// @param commsCoreIF reference to the CommsCore interface
    void registerChannel(CommsCoreIF& commsCoreIF);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the RSSI (Received Signal Strength Indicator) value for the BLE link
    /// @param isValid returns true if the RSSI value is valid
    /// @return The RSSI value in dBm.
    double getRSSI(bool& isValid);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if the BLE server is connected
    /// @return true if the BLE server is connected
    bool isConnected() const
    {
        return _isConnected;
    }

private:
    // Singleton (used for event callbacks)
    static BLEGapServer* _pThis;

    // Comms core interface
    CommsCoreIF* _pCommsCoreIF = nullptr;

    // Config
    BLEConfig _bleConfig;

    // BLE device initialised
    bool _isInit = false;

    // Get advertising name function
    GetAdvertisingNameFnType _getAdvertisingNameFn = nullptr;
    static const uint32_t BLE_GAP_MAX_ADV_NAME_LEN = 31;

    // Status change function
    StatusChangeFnType _statusChangeFn = nullptr;
    
    // Addr type - this is filled in when the BLE stack is synchronised
    // Possible types are:
    // BLE_OWN_ADDR_PUBLIC - Public address (48 bit like a MAC address and unique to each device)
    // BLE_OWN_ADDR_RANDOM - Random address for privacy (48 bit but stays the same across connections)
    // BLE_OWN_ADDR_RPA_RANDOM_DEFAULT - Random address for privacy (48 bit but changes regularly)
    // BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT - Public address (48 bit like a MAC address and unique to each device)
    uint8_t _ownAddrType = 0;

    // Gatt server
    BLEGattServer _gattServer;

    // BLE advertisement decoder
    BLEAdvertDecoder _bleAdvertDecoder;

    // BLE Bus device manager
    RaftBusDevicesIF* _pBusDevicesIF = nullptr;

    // ChannelID used to identify this message channel to the CommsCoreIF
    uint32_t _commsChannelID = CommsCoreIF::CHANNEL_ID_UNDEFINED;

    // Status
    bool _isConnected = false;
    uint16_t _bleGapConnHandle = 0;

    // Cached RSSI value - updated regularly in loop()
    int8_t _rssi = 0;
    uint32_t _rssiLastMs = 0;
    static const uint32_t RSSI_CHECK_MS = 2000;

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

    // Check connection interval some time after connection
    bool _connIntervalCheckPending = false;
    uint32_t _connIntervalCheckPendingStartMs = 0;
    static const uint32_t CONN_INTERVAL_CHECK_MS = 200;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Start BLE advertising
    /// @return true if advertising started successfully
    bool startAdvertising();
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Stop BLE advertising
    void stopAdvertising();
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Periodically check if BLE advertising needs to be restarted
    void serviceTimedAdvertisingCheck();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Start BLE scanning
    /// @return true if scanning was started successfully
    bool startScanning();
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Callback function triggered when the BLE stack synchronization occurs
    static void onSyncStatic()
    {
        if (_pThis)
            _pThis->onSync();
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Callback function triggered when the BLE stack synchronization occurs
    void onSync();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Callback handler for GAP events in the BLE stack
    /// @param event the type of GAP event being signaled
    /// @return 0 if the event was handled successfully, or a non-zero value on failure
    int nimbleGapEvent(struct ble_gap_event *event);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set the connection state of the BLE server
    /// @param isConnected True if the BLE connection is established, false otherwise.
    /// @param connHandle The connection handle associated with the current BLE connection.
    void setConnState(bool isConnected, uint16_t connHandle = 0);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Callback function for GATT characteristic access
    /// @param characteristicName name of the GATT characteristic being accessed
    /// @param readOp true if the operation is a read; false if it is a write
    /// @param payloadbuffer pointer to the buffer containing the data being read or written
    /// @param payloadlength length of the data in the payload buffer
    void gattAccessCallback(const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if a restart of the BLE service is required and handles the restart process.
    /// @return true if a restart was in progress and handled
    bool loopRestartHandler();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Periodically retrieves the RSSI (Received Signal Strength Indicator) value and caches it
    void updateRSSICachedValue();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the name of a GAP event based on its event type
    /// @param eventType integer value representing the GAP event type
    /// @return string containing the name of the GAP event
    static String getGapEventName(int eventType);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Handle a GAP connection event
    int gapEventConnect(struct ble_gap_event *event, String& statusStr, int& connHandle);
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Handle a GAP disconnection event
    int gapEventDisconnect(struct ble_gap_event *event, String& statusStr, int& connHandle);
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Handle a GAP connection update event
    int gapEventConnUpdate(struct ble_gap_event *event, String& statusStr, int& connHandle);
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Handle a GAP repeat pairing event
    int gapEventRepeatPairing(struct ble_gap_event *event);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Handle a GAP discovery event
    int gapEventDiscovery(struct ble_gap_event *event, String& statusStr);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Handle a GAP discovery complete event
    int gapEventDiscComplete(struct ble_gap_event *event, String& statusStr);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Log detailed information about a BLE connection
    static void debugLogConnInfo(const char* prefix, struct ble_gap_conn_desc *desc);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Debug log discovery event details
    /// @param event pointer to the `ble_gap_event` structure containing the discovery event details
    static void debugLogDiscEvent(const char* prefix, struct ble_gap_event *event);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert BLE address to a BusELemAddrType
    /// @param bleAddr BLE address
    /// @return BusElemAddrType
    BusElemAddrType convertToBusAddr(ble_addr_t bleAddr)
    {
        return ((bleAddr.val[5] ^ bleAddr.val[4] ^ bleAddr.val[3]) << 24) | 
            (bleAddr.val[2] << 16) | (bleAddr.val[1] << 8) | bleAddr.val[0];
    }

    // Message sending
    bool isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
    bool sendBLEMsg(CommsChannelMsg& msg);

    // Task
    static void bleHostTask(void *param);
    static void print_addr(const uint8_t *addr);
    bool nimbleStart();
    bool nimbleStop();
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
    void loop() {}

    // Get status JSON
    String getStatusJSON(bool includeBraces, bool shortForm) const
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

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "BLEGapSrv";
};
