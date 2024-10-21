/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGapServer
// Handles BLE GAP
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "sdkconfig.h"
#ifdef CONFIG_BT_ENABLED

#include "BLEGapServer.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"
#include "CommsChannelSettings.h"
#include "RaftUtils.h"
#include "ESPUtils.h"
#include "BLEAdDecode.h"

#undef min
#undef max
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_nimble_hci.h"
#endif
#undef min
#undef max

// Use a separate thread to send over BLE
// #define USE_SEPARATE_THREAD_FOR_BLE_SENDING

// Send BLE message as soon as TX event occurs indicating notify
// #define SEND_BLE_MESSAGE_FROM_TX_EVENT

// Warn
#define WARN_ON_BLE_INIT_FAILED
#define WARN_ON_BLE_ADVERTISING_START_FAILURE
#define WARN_ON_ONSYNC_ADDR_ERROR
#define WARN_BLE_ON_RESET_EVENT
#define WARN_ON_BLE_ADV_DATA_PARSE_ERROR

// Debug
// #define DEBUG_BLE_SETUP
// #define DEBUG_BLE_ADVERTISING
// #define DEBUG_NIMBLE_START
// #define DEBUG_BLE_RX_PAYLOAD
// #define DEBUG_BLE_CONNECT
// #define DEBUG_BLE_GAP_EVENT
// #define DEBUG_RSSI_GET_TIME
// #define DEBUG_BLE_GAP_EVENT_RX_TX
// #define DEBUG_BLE_PERF_CALC_MIN
// #define DEBUG_BLE_PERF_CALC_FULL_MAY_AFFECT_MEASUREMENT
// #define DEBUG_BLE_ON_SYNC
// #define DEBUG_BLE_SCAN_START_STOP
// #define DEBUG_BLE_SCAN_EVENT

// Singleton instance
BLEGapServer* BLEGapServer::_pThis = nullptr;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor for BLEGapServer
/// @param getAdvertisingNameFn function pointer to get the advertising name
/// @param statusChangeFn function pointer to handle status changes
BLEGapServer::BLEGapServer(GetAdvertisingNameFnType getAdvertisingNameFn, 
                StatusChangeFnType statusChangeFn) :
      _gattServer([this](const char* characteristicName, bool readOp, std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> rxMsg)
                    {
                        return gattAccessCallback(characteristicName, readOp, rxMsg.data(), rxMsg.size());
                    },
                    _bleStats)
{
    _pThis = this;
    _getAdvertisingNameFn = getAdvertisingNameFn;
    _statusChangeFn = statusChangeFn;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor for BLEGapServer
BLEGapServer::~BLEGapServer()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup BLEGapServer
/// @param pCommsCoreIF pointer to the CommsCore interface (part of Raft libraries)
/// @param bleConfig configuration parameters for BLE
/// @return true if setup was successful
bool BLEGapServer::setup(CommsCoreIF* pCommsCoreIF, const BLEConfig& bleConfig)
{
    // Settings
    _bleConfig = bleConfig;
    _pCommsCoreIF = pCommsCoreIF;

    // Check if peripheral role enabled
    if (_bleConfig.enPeripheral)
    {
        // GATT server
        _gattServer.setup(_bleConfig);
    }
    
    // Start NimBLE if not already started
    if (!_isInit)
    {
        // Start NimBLE
        _isInit = true;
        if (!nimbleStart())
        {
            _isInit = false;
#ifdef WARN_ON_BLE_INIT_FAILED
            LOG_W(MODULE_PREFIX, "setup failed to start NimBLE");
            return false;
#endif
        }
    }

    // Currently disconnected
    setConnState(false);

#ifdef DEBUG_BLE_SETUP
        LOG_I(MODULE_PREFIX, "setup OK %s", bleConfig.debugStr().c_str());
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Tears down the BLEGapServer, stopping advertising and deinitializing NimBLE
/// This function ensures that BLE services are properly stopped before shutting down the server
void BLEGapServer::teardown()
{
    // Check initialised
    if (!_isInit)
        return;

    // Stop advertising
    stopAdvertising();

    // Stop GATTServer
    _gattServer.stop();

    // Remove callbacks etc
    ble_hs_cfg.store_status_cb = NULL;
    ble_hs_cfg.gatts_register_cb  = NULL;
    ble_hs_cfg.sync_cb = NULL;
    ble_hs_cfg.reset_cb = NULL;

    // Deinitialize
    nimble_port_deinit();
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_err_t err = esp_nimble_hci_and_controller_deinit();
    if (err != ESP_OK)
    {
#ifdef WARN_ON_BLE_INIT_FAILED
        LOG_W(MODULE_PREFIX, "applySetup deinit failed");
#endif
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Services the BLEGapServer by handling advertising, GATT server, and RSSI polling
/// This method should be called frequently from the main loop task to keep the BLE services running and responsive.
void BLEGapServer::loop()
{
    // Check we are initialised
    if (!_isInit)
        return;

    // Loop over restart handler
    if (loopRestartHandler())
        return;

    // Service timed advertising check
    serviceTimedAdvertisingCheck();

    // Service GATT server
    _gattServer.loop();

    // Update cached RSSI value
    updateRSSICachedValue();

    // Check connection interval some time after connection
    if (_connIntervalCheckPending && 
            Raft::isTimeout(millis(), _connIntervalCheckPendingStartMs, CONN_INTERVAL_CHECK_MS))
    {
        // Request conn interval we want
        requestConnInterval();
        _connIntervalCheckPending = false;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Restart the BLEGapServer (by stopping and restarting the BLE stack)
void BLEGapServer::restart()
{
    // Stop advertising
    stopAdvertising();

    // Set state to stop required
    _bleRestartState = BLERestartState_StopRequired;
    _bleRestartLastMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register BLEGapServer as a communication channel with the Raft CommsCore interface
/// @param commsCoreIF reference to the CommsCore interface
void BLEGapServer::registerChannel(CommsCoreIF& commsCoreIF)
{
    // Comms channel
    uint32_t maxPktLen = _gattServer.getMaxPacketLen();
    const CommsChannelSettings commsChannelSettings(maxPktLen, maxPktLen, 0, 0, maxPktLen, 0);

    // Register as a message channel
    _commsChannelID = commsCoreIF.registerChannel("RICSerial", 
            "BLE",
            "BLE",
            std::bind(&BLEGapServer::sendBLEMsg, this, std::placeholders::_1),
            std::bind(&BLEGapServer::isReadyToSend, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            &commsChannelSettings);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the RSSI (Received Signal Strength Indicator) value for the BLE link
/// @param isValid returns true if the RSSI value is valid
/// @return The RSSI value in dBm.
double BLEGapServer::getRSSI(bool& isValid)
{
    isValid = _isConnected && (_rssi != 0);
    return _rssi;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the status of the BLEGapServer as a JSON string
/// @param includeBraces if true, the JSON string will be enclosed in braces
/// @param shortForm if true, the JSON string will be in a short form
/// @return The status of the BLEGapServer as a JSON string
String BLEGapServer::getStatusJSON(bool includeBraces, bool shortForm) const
{
    // Status result
    String statusStr;

    // RSSI
    String rssiStr = _isConnected ? R"("rssi":)" + String(_rssi) : "";

    // Check format
    if (shortForm)
    {
        // Connection info
        bool gapConn = ble_gap_conn_active();
        bool isAdv = ble_gap_adv_active();
        bool isDisco = ble_gap_disc_active();
        String connStr = String(R"("s":")") + 
                (_isConnected ? (gapConn ? "actv" : "conn") : (isAdv ? "adv" : (isDisco ? "disco" : "none"))) + 
                R"(")";

        // Advertising name
        String advNameStr = isAdv ? R"("adv":")" + String(ble_svc_gap_device_name()) + R"(")" : "";

        // Status str
        statusStr = connStr;
        if (!advNameStr.isEmpty())
            statusStr += "," + advNameStr;
        if (!rssiStr.isEmpty())
            statusStr += "," + rssiStr;
    }
    else
    {
        // Advertising
        bool advertisingActive = ble_gap_adv_active();
        String isAdvStr = R"("isAdv":)" + String(advertisingActive ? 1 : 0);
        String advNameStr = advertisingActive ? R"("advName":")" + String(ble_svc_gap_device_name()) : "";

        // Discovery active
        String isDiscoveryStr = R"("isDisc":)" + String(ble_gap_disc_active() ? 1 : 0);

        // Connection active
        String connStr = R"("isConn":)" + String(ble_gap_conn_active() ? 1 : 0);

        // BLE MAC address
        String bleMACStr = R"("BLEMAC":")" + getSystemMACAddressStr(ESP_MAC_BT, ":") + R"(")";

        // Status
        statusStr = connStr + "," + isAdvStr;
        if (!advNameStr.isEmpty())
            statusStr += "," + advNameStr;
        if (!rssiStr.isEmpty())
            statusStr += "," + rssiStr;
        statusStr += "," + bleMACStr;
    }

    // Add stats
    statusStr += "," + _bleStats.getJSON(false, shortForm);

    // Return
    if (includeBraces)
        return "{" + statusStr + "}";
    return statusStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback function triggered when the BLE stack synchronization occurs
/// Validate the device's BLE address and start advertising (if not required)
void BLEGapServer::onSync()
{
#ifdef DEBUG_BLE_ON_SYNC
    LOG_I(MODULE_PREFIX, "onSync isInit %d isCentral %d isPeripheral %d", _isInit, _bleConfig.enCentral, _bleConfig.enPeripheral);
#endif

    if (!_isInit)
        return;

    // Validate address
    ble_hs_util_ensure_addr(0);

    // Check if peripheral mode is enabled
    if (_bleConfig.enPeripheral)
    {
        // Figure out address to use while advertising (no privacy for now)
        int rc = ble_hs_id_infer_auto(0, &_ownAddrType);
        if (rc != NIMBLE_RETC_OK)
        {
#ifdef WARN_ON_ONSYNC_ADDR_ERROR
            LOG_W(MODULE_PREFIX, "onSync() error determining address type; rc=%d", rc);
#endif
            return;
        }

        // Debug showing addr
        uint8_t addrVal[6] = {0};
        rc = ble_hs_id_copy_addr(_ownAddrType, addrVal, NULL);
#ifdef DEBUG_BLE_CONNECT
        LOG_I(MODULE_PREFIX, "onSync() Device Address: %x:%x:%x:%x:%x:%x",
                addrVal[5], addrVal[4], addrVal[3], addrVal[2], addrVal[1], addrVal[0]);
#endif

        // Start advertising
        if (!startAdvertising())
        {
#ifdef WARN_ON_BLE_ADVERTISING_START_FAILURE
            LOG_W(MODULE_PREFIX, "onSync started advertising FAILED");
#endif
        }
    }

    // Check if central mode is enabled
    if (_bleConfig.enCentral)
    {
        // Start scanning
        startScanning();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Start BLE advertising
/// It uses a general discoverable mode and undirected connectable mode. The advertising interval is
/// set if specified in the configuration
/// @return true if advertising started successfully
bool BLEGapServer::startAdvertising()
{
    if (!_isInit)
        return false;

    // Check if already advertising
    if (ble_gap_adv_active())
    {
#ifdef DEBUG_BLE_ADVERTISING
        LOG_I(MODULE_PREFIX, "startAdvertising - already advertising");
#endif
        return true;
    }

    // Set advertising data
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof fields);

    // Flags: discoverable, BLE-only
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    // Include tx power level (filled automatically)
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // 128 bit UUID
    fields.uuids128 = &_gattServer.getMainServiceUUID128();
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    // Set the advertising data
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != NIMBLE_RETC_OK)
    {
        LOG_E(MODULE_PREFIX, "error setting adv fields; rc=%d", rc);
        return false;
    }

    // Clear fields
    memset(&fields, 0, sizeof fields);

    // Set the advertising name
    if (_getAdvertisingNameFn)
    {
        if (ble_svc_gap_device_name_set(_getAdvertisingNameFn().c_str()) != NIMBLE_RETC_OK)
        {
            LOG_E(MODULE_PREFIX, "error setting adv name rc=%d", rc);
        }
    }

    // Set the advertising response data
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strnlen(name, BLE_GAP_MAX_ADV_NAME_LEN);
    fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != NIMBLE_RETC_OK)
    {
        LOG_E(MODULE_PREFIX, "error setting adv rsp fields; rc=%d", rc);
        return false;
    }

    // Setup advertising name and rate
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;


    // Check if advertising interval is specified
    if (_bleConfig.advertisingIntervalMs > 0)
    {
        // Set advertising interval
        uint16_t advIntv = _bleConfig.advertisingIntervalMs / 0.625;
        adv_params.itvl_min = advIntv;
        adv_params.itvl_max = advIntv;
    }

    // Start advertising
    rc = ble_gap_adv_start(_ownAddrType, NULL, BLE_HS_FOREVER,
                           &adv_params,
                           [](struct ble_gap_event *event, void *arg) {
                                 return ((BLEGapServer*)arg)->nimbleGapEvent(event);
                           }, 
                           this);
    if (rc != NIMBLE_RETC_OK)
    {
        LOG_E(MODULE_PREFIX, "error enabling adv; rc=%d", rc);
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Stop BLE advertising
void BLEGapServer::stopAdvertising()
{
    if (!_isInit)
        return;
    ble_gap_adv_stop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback handler for GAP events in the BLE stack
/// The NimBLE host calls this function when a GAP (Generic Access Profile) event occurs.
/// It handles various GAP events such as connection, disconnection, advertisement complete,
/// encryption changes, notification transmission, subscription updates, and others
/// The same callback is used for all BLE connections
///
/// @param event the type of GAP event being signaled, which can include events such as connection,
///              disconnection, advertisement completion, etc.
/// @return 0 if the event was handled successfully, or a non-zero value on failure, depending on the event type.
int BLEGapServer::nimbleGapEvent(struct ble_gap_event *event)
{
    int connHandle = -1;
    String statusStr="OK";
    int errorCode = 0;

    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT: 
            errorCode = gapEventConnect(event, statusStr, connHandle);
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            errorCode = gapEventDisconnect(event, statusStr, connHandle);
            break;
        case BLE_GAP_EVENT_CONN_UPDATE:
            errorCode = gapEventConnUpdate(event, statusStr, connHandle);
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            statusStr = (event->adv_complete.reason == 0 ? String("new-conn") : 
                                    BLEGattServer::getHSErrorMsg(event->adv_complete.reason));
            errorCode = startAdvertising();
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            statusStr = BLEGattServer::getHSErrorMsg(event->enc_change.status);
            connHandle = event->enc_change.conn_handle; 
            break;
        case BLE_GAP_EVENT_NOTIFY_TX:
            statusStr = BLEGattServer::getHSErrorMsg(event->notify_tx.status);
            _gattServer.getOutbound().notifyTxComplete(event->notify_tx.status);
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            // Handle subscription to GATT attr
            _gattServer.handleSubscription(event, statusStr);
            break;
        case BLE_GAP_EVENT_MTU:
            statusStr = "mtu:" + String(event->mtu.value) + ",chanID:" + String(event->mtu.channel_id);
            _gattServer.getOutbound().onMTUSizeInfo(event->mtu.value);
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            errorCode = gapEventRepeatPairing(event);
            break;
        case BLE_GAP_EVENT_DISC:
            errorCode = gapEventDisc(event, statusStr);
            break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            errorCode = gapEventDiscComplete(event, statusStr);
            break;
    }

#ifdef DEBUG_BLE_GAP_EVENT
#ifndef DEBUG_BLE_GAP_EVENT_RX_TX
    if ((event->type != BLE_GAP_EVENT_NOTIFY_TX) && (event->type != BLE_GAP_EVENT_NOTIFY_RX)) {
#endif
    LOG_I(MODULE_PREFIX, "GAPEv %s connHandle=%s status=%s errorCode=%s",
                getGapEventName(event->type).c_str(), 
                connHandle >= 0 ? String(connHandle).c_str() : "N/A", 
                statusStr.c_str(), 
                BLEGattServer::getHSErrorMsg(errorCode).c_str());
    if (connHandle >= 0)
    {
        // Log connection info
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(connHandle, &desc) == 0)
            debugLogConnInfo("GAPConnInfo ", &desc);
    }
#ifndef DEBUG_BLE_GAP_EVENT_RX_TX
    }
#endif
#endif

    return errorCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief BLE host task
/// This function runs the NimBLE host task, which handles the BLE stack's internal processing. 
/// It runs in a FreeRTOS task context and will not return until `nimble_port_stop()` is executed.
/// After the task ends, it deinitializes the NimBLE port.
/// @param param Pointer to a parameter passed to the task (unused in this implementation).
void BLEGapServer::bleHostTask(void *param)
{
    // This function will return only when nimble_port_stop() is executed
#ifdef DEBUG_BLE_SETUP
    LOG_I(MODULE_PREFIX, "BLE Host Task Started");
#endif
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback function for GATT characteristic access
/// This function is called when a GATT read or write operation is performed on a characteristic.
/// It handles test frames used to determine link performance, and for write operations, it sends
/// the received message to the communication core interface
/// @param characteristicName name of the GATT characteristic being accessed
/// @param readOp true if the operation is a read; false if it is a write
/// @param payloadbuffer pointer to the buffer containing the data being read or written
/// @param payloadlength length of the data in the payload buffer
void BLEGapServer::gattAccessCallback(const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength)
{
    // Check for test frames (used to determine performance of the link)
    if ((payloadlength > 10) && (payloadbuffer[5] == 0x1f) && (payloadbuffer[6] == 0x9d) && (payloadbuffer[7] == 0xf4) && (payloadbuffer[8] == 0x7a) && (payloadbuffer[9] == 0xb5))
    {
        // Get msg count
        uint32_t inMsgCount = (payloadbuffer[1] << 24)
                    | (payloadbuffer[2] << 16)
                    | (payloadbuffer[3] << 8)
                    | payloadbuffer[4];
        bool isSeqOk = inMsgCount == _lastTestMsgCount+1;
        bool isFirst = inMsgCount == 0;
        if (isFirst)
        {
            _testPerfPrbsState = 1;
            _bleStats.clearTestPerfStats();
            isSeqOk = true;
        }
        _lastTestMsgCount = inMsgCount;
        
        // Check test message valid
        bool isDataOk = true;
        for (uint32_t i = 10; i < payloadlength; i++) {
            _testPerfPrbsState = Raft::parkMillerNext(_testPerfPrbsState);
            if (payloadbuffer[i] != (_testPerfPrbsState & 0xff)) {
                isDataOk = false;
            }
        }

        // Update test stats
        _bleStats.rxTestFrame(payloadlength, isSeqOk, isDataOk);

        // Debug
#if defined(DEBUG_BLE_PERF_CALC_FULL_MAY_AFFECT_MEASUREMENT)
        LOG_I(MODULE_PREFIX, "%s inMsgCount %4d rate %8.1fBytesPS seqErrs %4d dataErrs %6d  len %4d", 
                    isFirst ? "FIRST  " : (isSeqOk & isDataOk) ? "OK     " : !isSeqOk ? "SEQERR " : "DATERR ",
                    inMsgCount+1,
                    _bleStats.getTestRate(),
                    _bleStats.getTestSeqErrCount(),
                    _bleStats.getTestDataErrCount(),
                    payloadlength);
#elif defined(DEBUG_BLE_PERF_CALC_MIN)
        LOG_I(MODULE_PREFIX, "%s %.1fBPS", 
                    isFirst ? "F" : (isSeqOk & isDataOk) ? "K" : !isSeqOk ? "Q" : "D",
                    _bleStats.getTestRate());
#endif
    }
    else
    {
        _bleStats.rxMsg(payloadlength);
    }
    if (!readOp)
    {
        // Send the message to the comms channel if this is a write to the characteristic
        if (_pCommsCoreIF)
            _pCommsCoreIF->inboundHandleMsg(_commsChannelID, payloadbuffer, payloadlength);

#ifdef DEBUG_BLE_RX_PAYLOAD
        // Debug
        uint32_t sz = payloadlength;
        const uint8_t* pVals = payloadbuffer;
        String outStr;
        Raft::getHexStrFromBytes(pVals, sz, outStr);
        LOG_I(MODULE_PREFIX, "gatt rx payloadLen %d payload %s", sz, outStr.c_str());
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if the BLE server is ready to send a message
/// @param channelID identifier of the communication channel
/// @param msgType type of the message being sent
/// @param noConn parameter set to true if there is no valid BLE connection, false otherwise
/// @return true if the BLE server is ready to send a message
bool BLEGapServer::isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
{
    noConn = false;
    if (!_isInit || !_isConnected)
    {
        noConn = true;
        return false;
    }
    return _gattServer.isReadyToSend(channelID, msgType, noConn);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Sends a message over BLE
/// @param msg message to be sent over BLE, encapsulated in a `CommsChannelMsg` object
/// @return true if the message was successfully sent
bool BLEGapServer::sendBLEMsg(CommsChannelMsg& msg)
{
    if (!_isInit)
        return false;
    return _gattServer.sendMsg(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the connection state of the BLE server
/// This function updates the internal state to reflect whether a BLE connection is active and sets the connection handle.
/// It also updates the GATT server's connection state and notifies any registered status change handlers.
/// @param isConnected True if the BLE connection is established, false otherwise.
/// @param connHandle The connection handle associated with the current BLE connection.
void BLEGapServer::setConnState(bool isConnected, uint16_t connHandle)
{
#ifdef USE_TIMED_ADVERTISING_CHECK
    // Reset timer for advertising check
    _advertisingCheckRequired = !isConnected;
    _advertisingCheckMs = millis();
#endif

    // Connected state change
    _isConnected = isConnected;
    _bleGapConnHandle = connHandle;

    // Set into GATT
    _gattServer.setConnState(isConnected, connHandle);
    
    // Inform hooks of status change
    if (_statusChangeFn)
        _statusChangeFn(isConnected);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Starts the BLE stack and initializes the NimBLE port
/// This function starts the NimBLE BLE stack and sets up necessary callbacks for events like reset and synchronization.
/// It configures the GATT server and starts the FreeRTOS task that runs the NimBLE host.
/// If an error occurs during initialization, the function returns false.
/// @return true if the BLE stack was successfully started
bool BLEGapServer::nimbleStart()
{
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    // Init Nimble
    esp_err_t err = esp_nimble_hci_and_controller_init();
    if (err != ESP_OK)
    {
#ifdef WARN_ON_BLE_INIT_FAILED
        LOG_E(MODULE_PREFIX, "nimbleStart esp_nimble_hci_and_controller_init failed err=%d", err);
#endif
        return false;
    }
#endif

    // Log level for NimBLE module is set in here so if we want to override it
    // we have to do so after this call
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
    {
#ifdef WARN_ON_BLE_INIT_FAILED
        LOG_E(MODULE_PREFIX, "nimbleStart nimble_port_init failed err=%d", err);
#endif
        return false;
    }

    // onReset callback
    ble_hs_cfg.reset_cb = [](int reason) {
#ifdef WARN_BLE_ON_RESET_EVENT
        LOG_I(MODULE_PREFIX, "onReset() reason=%d", reason);
#endif
    };

    // onSync callback
    ble_hs_cfg.sync_cb = onSyncStatic;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Check if gatt server is to be used (peripheral role)
    if (_bleConfig.enPeripheral)
    {
        // Callback for GATT registration
        ble_hs_cfg.gatts_register_cb = _bleConfig.enPeripheral ? BLEGattServer::registrationCallbackStatic : nullptr;

        // Settings to use for pairing
        ble_hs_cfg.sm_io_cap = _bleConfig.pairingSMIOCap;
        ble_hs_cfg.sm_sc = _bleConfig.pairingSecureConn;

        // Initialize the GATT server
        int rc = _gattServer.start();
        if (rc == 0)
        {
            // Set the advertising name
            int rcAdv = -1;
            if (_getAdvertisingNameFn)
                rcAdv = ble_svc_gap_device_name_set(_getAdvertisingNameFn().c_str());

#ifdef DEBUG_NIMBLE_START
            LOG_I(MODULE_PREFIX, "nimbleStart OK advNameSetOk=%s (%d)", rcAdv == -1 ? "no name specified" : (rcAdv == 0 ? "OK" : "FAILED"), rcAdv);
#else
            (void)rcAdv;
#endif
        }
        else
        {
            LOG_W(MODULE_PREFIX, "nimbleStart _gattServer.initServer() failed rc=%d", rc);
        }
    }

    // Start the host task
    nimble_port_freertos_init(bleHostTask);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Stop the BLE stack and deinitialize the NimBLE port
bool BLEGapServer::nimbleStop()
{

    esp_err_t err = nimble_port_stop();
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "nimbleStop nimble_port_stop() failed esp_err=%d", err);
        return false;
    }
    nimble_port_deinit();

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    ret = esp_nimble_hci_and_controller_deinit();
    if (ret != 0) {
        LOG_W(MODULE_PREFIX, "nimbleStop nimble_port_deinit() failed ret=%d", ret);
        return false;
    }
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Log detailed information about a BLE connection
/// @param prefix string prefix for the log message
/// @param desc pointer to a `ble_gap_conn_desc` structure containing detailed information about the connection
void BLEGapServer::debugLogConnInfo(const char* prefix, struct ble_gap_conn_desc *desc)
{
    LOG_I(MODULE_PREFIX, "%shdl=%d Itvl %d Latcy %d Timo %d Enc %d Auth %d Bond %d OurOTA(%d) %s OurID(%d) %s PeerOTA(%d) %s PeerID(%d) %s", 
                    prefix,
                    desc->conn_handle, 
                    desc->conn_itvl,
                    desc->conn_latency,
                    desc->supervision_timeout,
                    desc->sec_state.encrypted,
                    desc->sec_state.authenticated,
                    desc->sec_state.bonded,
                    desc->our_ota_addr.type, Raft::formatMACAddr(desc->our_ota_addr.val, ":").c_str(),
                    desc->our_id_addr.type, Raft::formatMACAddr(desc->our_id_addr.val, ":").c_str(),
                    desc->peer_ota_addr.type, Raft::formatMACAddr(desc->peer_ota_addr.val, ":").c_str(),
                    desc->peer_id_addr.type, Raft::formatMACAddr(desc->peer_id_addr.val, ":").c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Debug log discovery event details
/// @param event pointer to the `ble_gap_event` structure containing the discovery event details
void BLEGapServer::debugLogDiscEvent(const char* prefix, struct ble_gap_event *event)
{
    String hexStr;
    Raft::getHexStrFromBytes(event->disc.data, event->disc.length_data, hexStr);

    // Log discovery event
    LOG_I(MODULE_PREFIX, "%saddr %s (type %d) event %s rssi %d data %s",
                prefix,
                Raft::formatMACAddr(event->disc.addr.val, ":", true).c_str(),
                event->disc.addr.type,
                getGapEventName(event->type).c_str(),
                event->disc.rssi,
                hexStr.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the name of a GAP event based on its event type
/// @param eventType integer value representing the GAP event type
/// @return string containing the name of the GAP event
String BLEGapServer::getGapEventName(int eventType)
{
    switch (eventType)
    {
        case BLE_GAP_EVENT_CONNECT: return "CONNECT";
        case BLE_GAP_EVENT_DISCONNECT: return "DISCONNECT";
        case BLE_GAP_EVENT_CONN_UPDATE: return "CONN_UPDATE";
        case BLE_GAP_EVENT_CONN_UPDATE_REQ: return "CONN_UPDATE_REQ";
        case BLE_GAP_EVENT_L2CAP_UPDATE_REQ: return "L2CAP_UPDATE_REQ";
        case BLE_GAP_EVENT_TERM_FAILURE: return "TERM_FAILURE";
        case BLE_GAP_EVENT_DISC: return "DISCOVERY";
        case BLE_GAP_EVENT_DISC_COMPLETE: return "DISCOVERY_COMPLETE";
        case BLE_GAP_EVENT_ADV_COMPLETE: return "ADV_COMPLETE";
        case BLE_GAP_EVENT_ENC_CHANGE: return "ENC_CHANGE";
        case BLE_GAP_EVENT_PASSKEY_ACTION: return "PASSKEY_ACTION";
        case BLE_GAP_EVENT_NOTIFY_RX: return "NOTIFY_RX";
        case BLE_GAP_EVENT_NOTIFY_TX: return "NOTIFY_TX";
        case BLE_GAP_EVENT_SUBSCRIBE: return "SUBSCRIBE";
        case BLE_GAP_EVENT_MTU: return "MTU";
        case BLE_GAP_EVENT_IDENTITY_RESOLVED: return "IDENTITY_RESOLVED";
        case BLE_GAP_EVENT_REPEAT_PAIRING: return "REPEAT_PAIRING";
        case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE: return "PHY_UPDATE_COMPLETE";
        case BLE_GAP_EVENT_EXT_DISC: return "EXT_DISC";
        default: return "UNKNOWN (" + String(eventType) + ")";
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle a GAP connection event when a new BLE connection is established or a connection attempt fails
/// This function processes the connection event, updates the connection handle, and sets the connection state.
/// If the connection is successful, it sets the preferred MTU size and connection parameters. If the connection fails,
/// it attempts to resume advertising.
/// @param event GAP event structure containing details about the connection event
/// @param statusStr string reference that will be updated with the connection status (e.g., "conn-ok" or "conn-fail")
/// @param connHandle reference to the connection handle that will be updated if the connection is established
/// @return NIMBLE_RETC_OK if the event was handled successfully, or a NimBLE error code defined in host/ble_hs.h on error
int BLEGapServer::gapEventConnect(struct ble_gap_event *event, String& statusStr, int& connHandle)
{
    // A new connection was established or a connection attempt failed
    int rc = NIMBLE_RETC_OK;
    if (event->connect.status == 0)
    {
        // Return values
        statusStr = "conn-ok";
        connHandle = event->connect.conn_handle;
        
        // Request preferred MTU
        rc = ble_att_set_preferred_mtu(_gattServer.getPreferredMTUSize());
        if (rc != NIMBLE_RETC_OK) 
        {
            LOG_W(MODULE_PREFIX, "nimbleGAPEvent conn failed to set preferred MTU; rc = %d", rc);
        }

        // Preferred PHY (2M if supported)
#if IDF_TARGET_ESP32S3
        ble_gap_set_prefered_default_le_phy(BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK);
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        // Suggested DLE
        ble_hs_hci_util_write_sugg_def_data_len(_bleConfig.llPacketLengthPref, _bleConfig.llPacketTimePref);

        // Suggested values for read DLE
        uint16_t out_sugg_max_tx_octets = 0, out_sugg_max_tx_time = 0;
        rc = ble_hs_hci_util_read_sugg_def_data_len(&out_sugg_max_tx_octets, &out_sugg_max_tx_time);
        if (rc != NIMBLE_RETC_OK)
        {
            LOG_W(MODULE_PREFIX, "nimbleGAPEvent conn failed to read suggested data len; rc = %d", rc);
        }
        else
        {
            LOG_I(MODULE_PREFIX, "nimbleGAPEvent conn suggested data len tx %d time %d", out_sugg_max_tx_octets, out_sugg_max_tx_time);
        }
#else
        // Old way of setting DLE
        ble_hs_hci_util_set_data_len(event->connect.conn_handle,
                            _bleConfig.llPacketLengthPref,
                            _bleConfig.llPacketTimePref);
#endif
        // Conn interval check pending
        _connIntervalCheckPending = true;
        _connIntervalCheckPendingStartMs = millis();
        
        // Now connected
        setConnState(true, event->connect.conn_handle);
    }
    else
    {
        // Return values
        statusStr = "conn-fail";

        // Not connected
        setConnState(false);

        // Check if peripheral mode is enabled
        if (_bleConfig.enPeripheral)
        {
            // Connection failed; resume advertising
            if (!startAdvertising())
            {
#ifdef WARN_ON_BLE_ADVERTISING_START_FAILURE
                LOG_W(MODULE_PREFIX, "nimbleGAPEvent conn start advertising FAILED");
#endif
            }
            else
            {
                LOG_I(MODULE_PREFIX, "GAPEvent conn resumed advertising after connection failure");
            }
        }
    }
    return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle GAP disconnection event when a BLE connection is terminated
/// This function processes the disconnection event, logs the disconnection reason, and sets the connection state to disconnected
/// It then restarts advertising if appropriate based on configuration.
/// @param event GAP event structure containing details about the disconnection event, including the reason for disconnection
/// @param statusStr string reference that will be updated with the disconnection status and reason
/// @param connHandle reference to the connection handle that will be updated with the handle of the disconnected connection
/// @return NIMBLE_RETC_OK if the event was handled successfully
int BLEGapServer::gapEventDisconnect(struct ble_gap_event *event, String& statusStr, int& connHandle)
{
    // Return values
    statusStr = "disconn reason " + BLEGattServer::getHSErrorMsg(event->disconnect.reason);
    connHandle = event->disconnect.conn.conn_handle;

    // Connection terminated
    setConnState(false);

    // Check if we should restart - peripheral mode
    if (_bleConfig.enPeripheral)
    {
        // Note that if USE_TIMED_ADVERTISING_CHECK is defined then
        // advertising will restart due to check in loop()
#ifndef USE_TIMED_ADVERTISING_CHECK
        // Restart advertising
        startAdvertising();
#endif
    }
    return NIMBLE_RETC_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle a GAP connection update event, which occurs when the connection parameters (e.g., interval, latency) are updated.
/// This function processes the connection update, checks if the new connection interval meets the preferred interval,
/// and requests a new connection interval if necessary.
/// @param event GAP event structure containing details about the connection update, including the updated parameters.
/// @param statusStr string reference that will be updated with the status of the connection update.
/// @param connHandle reference to the connection handle that will be updated with the handle of the updated connection.
/// @return NIMBLE_RETC_OK if the event was handled successfully.
int BLEGapServer::gapEventConnUpdate(struct ble_gap_event *event, String& statusStr, int& connHandle)
{
    // Status
    statusStr = BLEGattServer::getHSErrorMsg(event->conn_update.status);
    // Check if conn interval is greater than the one we want
    connHandle = event->conn_update.conn_handle;
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
    if ((rc == NIMBLE_RETC_OK) && _connIntervalCheckPending && (desc.conn_itvl != _bleConfig.getConnIntervalPrefBLEUnits()))
    {
        // Request conn interval we want
        requestConnInterval();
    }
    _connIntervalCheckPending = false;
    return NIMBLE_RETC_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle a GAP repeat pairing event, which occurs when the peer device attempts to establish a new secure link despite already being paired.
/// This function handles the repeat pairing request by deleting the old bond with the peer and allowing the new pairing to proceed.
/// @param event GAP event structure containing details about the repeat pairing event.
/// @return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the pairing operation should continue.
int BLEGapServer::gapEventRepeatPairing(struct ble_gap_event *event)
{
    // We already have a bond with the peer, but it is attempting to
    // establish a new secure link.  This app sacrifices security for
    // convenience: just throw away the old bond and accept the new link.
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
        ble_store_util_delete_peer(&desc.peer_id_addr);

    // Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
    // continue with the pairing operation.
    return BLE_GAP_REPEAT_PAIRING_RETRY;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle a GAP discovery event, which occurs when the BLE stack is actively scanning for devices
/// This function processes the discovery event and logs the discovery status.
/// @param event GAP event structure containing details about the discovery event.
/// @param statusStr string reference that will be updated with the status of the discovery event.
/// @return NIMBLE_RETC_OK if the event was handled successfully.
int BLEGapServer::gapEventDisc(struct ble_gap_event *event, String& statusStr)
{
    // Parse the advertisement data
    struct ble_hs_adv_fields fields;
    int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                    event->disc.length_data);
    if (rc != NIMBLE_RETC_OK) {
#ifdef WARN_ON_BLE_ADV_DATA_PARSE_ERROR
        LOG_W(MODULE_PREFIX, "gapEventDisc FAILED to parse advertisement data; rc=%d", rc);
#endif
        return rc;
    }

    // Check if BTHome is supported - in which case decode the packet
    if (_bleConfig.scanBTHome)
    {
        BLEAdDecode::decodeAdEvent(event, fields);
    }

    // Debug
#ifdef DEBUG_BLE_SCAN_EVENT
    debugLogDiscEvent("GAPDiscInfo ", event);
#endif

    // Debug
    return NIMBLE_RETC_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle a GAP discovery complete event, which occurs when the BLE stack has completed a device discovery operation
/// This function processes the discovery complete event and logs the discovery status.
/// @param event GAP event structure containing details about the discovery complete event.
/// @param statusStr string reference that will be updated with the status of the discovery completion.
/// @return NIMBLE_RETC_OK if the event was handled successfully.
int BLEGapServer::gapEventDiscComplete(struct ble_gap_event *event, String& statusStr)
{
    // Debug
#ifdef DEBUG_BLE_SCAN_START_STOP
    LOG_I(MODULE_PREFIX, "gapEventDiscComplete status=%d", event->disc_complete.reason);
#endif

    return NIMBLE_RETC_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if a restart of the BLE service is required and handles the restart process.
/// This function checks the current restart state and manages the stopping and restarting of the BLE stack if needed.
/// It handles timing to ensure that the BLE stack is properly stopped and restarted after a set delay.
/// @return true if a restart was in progress and handled
bool BLEGapServer::loopRestartHandler()

{
    // Check if restart in progress
    switch(_bleRestartState)
    {
        case BLERestartState_Idle:
            break;
        case BLERestartState_StopRequired:
            if (Raft::isTimeout(millis(), _bleRestartLastMs, BLE_RESTART_BEFORE_STOP_MS))
            {
                // Stop the BLE stack
                nimbleStop();
                _bleRestartState = BLERestartState_StartRequired;
                _bleRestartLastMs = millis();
            }
            break;
        case BLERestartState_StartRequired:
            if (Raft::isTimeout(millis(), _bleRestartLastMs, BLE_RESTART_BEFORE_START_MS))
            {
                // Start the BLE stack
                nimbleStart();
                _bleRestartState = BLERestartState_Idle;
                _bleRestartLastMs = millis();
            }
            return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Periodically check if BLE advertising needs to be restarted if the device is not connected.
/// This function handles the timed advertising check and ensures that advertising is restarted if it's not already active and the device is not connected.
/// It is expected to be called on the main task loop and uses a timeout mechanism to periodically perform the check and restart advertising if necessary.
void BLEGapServer::serviceTimedAdvertisingCheck()
{
#ifdef USE_TIMED_ADVERTISING_CHECK
    // Handle advertising check
    if (_bleConfig.enPeripheral && (!_isConnected) && (_advertisingCheckRequired))
    {
        if (Raft::isTimeout(millis(), _advertisingCheckMs, ADVERTISING_CHECK_MS))
        {
            _advertisingCheckMs = millis();
            // Check advertising
            if (!ble_gap_adv_active())
            {
                // Debug
#ifdef WARN_ON_BLE_ADVERTISING
                LOG_W(MODULE_PREFIX, "loop not conn or adv so start advertising");
#endif

                // Start advertising
                bool startAdvOk = startAdvertising();

                // Debug
#ifdef WARN_ON_BLE_ADVERTISING_START_FAILURE
                if (!startAdvOk)
                {
                    LOG_W(MODULE_PREFIX, "loop started advertising FAILED");
                }
#endif
#ifdef DEBUG_BLE_ADVERTISING
                if (startAdvOk)
                {
                    LOG_W(MODULE_PREFIX, "loop started advertising ok");
                }
#endif
            }
            else
            {
#ifdef DEBUG_BLE_ADVERTISING
                LOG_I(MODULE_PREFIX, "loop BLE already advertising");
#endif
                _advertisingCheckRequired = false;
            }
        }
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief periodically retrieves the RSSI (Received Signal Strength Indicator) value and caches it
/// It ensures that RSSI is polled at defined intervals to avoid overloading the system with frequent requests.
/// If the RSSI retrieval fails, the RSSI value is set to 0.
void BLEGapServer::updateRSSICachedValue()
{
    // Get RSSI value if connected - not too often as getting RSSI info can take ~2ms
    if (Raft::isTimeout(millis(), _rssiLastMs, RSSI_CHECK_MS))
    {
        _rssiLastMs = millis();
        _rssi = 0;
        if (_isConnected)
        {
#ifdef DEBUG_RSSI_GET_TIME
            uint64_t startUs = micros();
#endif
            int rslt = ble_gap_conn_rssi(_bleGapConnHandle, &_rssi);
#ifdef DEBUG_RSSI_GET_TIME
            uint64_t endUs = micros();
            LOG_I(MODULE_PREFIX, "loop get RSSI %d us", (int)(endUs - startUs));
#endif
            if (rslt != NIMBLE_RETC_OK)
            {
                _rssi = 0;
                // Debug
                // LOG_W(MODULE_PREFIX, "loop get RSSI failed");
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Request update to the BLE connection interval params based on the preferred connection parameters
/// This function sends a request to update the connection interval, latency, and supervision timeout to the preferred values.
void BLEGapServer::requestConnInterval()
{
    struct ble_gap_upd_params params;
    memset(&params, 0, sizeof(params));
    params.itvl_min = _bleConfig.getConnIntervalPrefBLEUnits();
    params.itvl_max = _bleConfig.getConnIntervalPrefBLEUnits();
    params.latency = _bleConfig.connLatencyPref;
    params.supervision_timeout = _bleConfig.supvTimeoutPrefMs / 10;
    params.min_ce_len = 0x0001;
    params.max_ce_len = 0x0001;
    int rc = ble_gap_update_params(_bleGapConnHandle, &params);
    if (rc != NIMBLE_RETC_OK)
    {
        LOG_W(MODULE_PREFIX, "requestConnInterval FAILED rc = %d", rc);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Start BLE scanning
/// This function starts BLE scanning in the central role to discover nearby BLE devices.
/// It uses the general discovery mode and sets the scanning interval and window to the specified values.
/// @return true if scanning was started successfully
bool BLEGapServer::startScanning()
{
    // TODO - REMOVE
    LOG_I(MODULE_PREFIX, "startScanning");

    if (!_isInit)
        return false;

    // Check if already scanning
    if (ble_gap_disc_active())
    {
#ifdef DEBUG_BLE_SCAN_START_STOP
        LOG_I(MODULE_PREFIX, "startScanning - already scanning");
#endif
        return true;
    }

    // Set scanning parameters
    struct ble_gap_disc_params disc_params;
    memset(&disc_params, 0, sizeof disc_params);
    disc_params.passive = _bleConfig.scanPassive;
    disc_params.itvl = _bleConfig.scanningIntervalMs / 0.625;
    disc_params.window = _bleConfig.scanningWindowMs / 0.625;
    disc_params.filter_duplicates = _bleConfig.scanNoDuplicates;

    // Check how long to scan for
    uint32_t scanForMs = ((uint32_t)_bleConfig.scanForSecs) * 1000;
    if (scanForMs == 0)
    {
        // Scan indefinitely
        scanForMs = INT32_MAX;
    }

    // Start scanning
    int rc = ble_gap_disc(_ownAddrType, scanForMs, &disc_params,
                          [](struct ble_gap_event *event, void *arg) {
                                return ((BLEGapServer*)arg)->nimbleGapEvent(event);
                          },
                          this);
    if (rc != NIMBLE_RETC_OK)
    {
        LOG_E(MODULE_PREFIX, "startScanning FAILED enabling scan; rc=%d", rc);
        return false;
    }

#ifdef DEBUG_BLE_SCAN_START_STOP
    LOG_I(MODULE_PREFIX, "startScanning - started OK");
#endif
    return true;
}

#endif // CONFIG_BT_ENABLED
