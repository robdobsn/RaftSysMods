/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGapServer
// Handles BLE GAP
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <sdkconfig.h>
#ifdef CONFIG_BT_ENABLED
#include "BLEGapServer.h"
#include <CommsCoreIF.h>
#include <CommsChannelMsg.h>
#include <CommsChannelSettings.h>
#include <RaftUtils.h>
#include <ESPUtils.h>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

// Use a separate thread to send over BLE
// #define USE_SEPARATE_THREAD_FOR_BLE_SENDING

// Send BLE message as soon as TX event occurs indicating notify
// #define SEND_BLE_MESSAGE_FROM_TX_EVENT

// Warn
#define WARN_ON_BLE_INIT_FAILED
#define WARN_ON_BLE_ADVERTISING_START_FAILURE
#define WARN_ON_ONSYNC_ADDR_ERROR
#define WARN_BLE_ON_RESET_EVENT

// Debug
#define DEBUG_BLE_SETUP
#define DEBUG_BLE_ADVERTISING
// #define DEBUG_BLE_RX_PAYLOAD
// #define DEBUG_BLE_TX_MSG
// #define DEBUG_BLE_TX_MSG_SPLIT
// #define DEBUG_BLE_TX_MSG_DETAIL
// #define DEBUG_BLE_PUBLISH
#define DEBUG_BLE_CONNECT
#define DEBUG_BLE_GAP_EVENT
// #define DEBUG_RSSI_GET_TIME

static const char* MODULE_PREFIX = "BLEGapServer";

// Singleton
BLEGapServer* BLEGapServer::_pThis = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BLEGapServer::BLEGapServer(GetAdvertisingNameFnType getAdvertisingNameFn, 
                StatusChangeFnType statusChangeFn) :
      _gattServer([this](const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength)
                    {
                        return gattAccessCallback(characteristicName, readOp, payloadbuffer, payloadlength);
                    },
                    _bleStats)
{
    _pThis  = this;
    _getAdvertisingNameFn = getAdvertisingNameFn;
    _statusChangeFn = statusChangeFn;
}

BLEGapServer::~BLEGapServer()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGapServer::setup(CommsCoreIF* pCommsCoreIF,
                uint32_t maxPacketLen, 
                uint32_t outboundQueueSize, bool useTaskForSending,
                UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize)
{
    // Settings
    _pCommsCoreIF = pCommsCoreIF;
    _maxPacketLength = maxPacketLen;

    // Setup GATT server
    _gattServer.setup(maxPacketLen, outboundQueueSize, useTaskForSending,
                taskCore, taskPriority, taskStackSize);
    
    // Start NimBLE if not already started
    if (!_isInit)
    {
        // Start NimBLE
        if (!nimbleStart())
            return false;
        _isInit = true;
    }

    // Currently disconnected
    setConnState(false);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Teardown
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGapServer::service()
{
    // Check we are initialised
    if (!_isInit)
        return;

    // Service restart if required
    if (serviceRestartIfRequired())
        return;

    // Service timed advertising check
    serviceTimedAdvertisingCheck();

    // Service GATT server
    _gattServer.service();

    // Service getting RSSI value
    serviceGettingRSSI();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Restart
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGapServer::restart()
{
    // Stop advertising
    stopAdvertising();

    // Set state to stop required
    _bleRestartState = BLERestartState_StopRequired;
    _bleRestartLastMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Register channel
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGapServer::registerChannel(CommsCoreIF& commsCoreIF)
{
    // Comms channel
    const CommsChannelSettings commsChannelSettings(_maxPacketLength, _maxPacketLength, 0, 0, _maxPacketLength, 0);

    // Register as a message channel
    _commsChannelID = commsCoreIF.registerChannel("RICSerial", 
            "BLE",
            "BLE",
            std::bind(&BLEGapServer::sendBLEMsg, this, std::placeholders::_1),
            std::bind(&BLEGapServer::isReadyToSend, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            &commsChannelSettings);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get RSSI
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double BLEGapServer::getRSSI(bool& isValid)
{
    isValid = _isConnected && (_rssi != 0);
    return _rssi;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get stats
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String BLEGapServer::getStatusJSON(bool includeBraces, bool shortForm)
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
        String isDiscoveryStr = R"(isDisc":)" + String(ble_gap_disc_active() ? 1 : 0);

        // Connection active
        String connStr = R"(isConn":)" + String(ble_gap_conn_active() ? 1 : 0);

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
// onSync callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGapServer::onSync()
{
    if (!_isInit)
        return;

    // Validate address
    ble_hs_util_ensure_addr(0);

    // Figure out address to use while advertising (no privacy for now)
    int rc = ble_hs_id_infer_auto(0, &_ownAddrType);
    if (rc != 0)
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// startAdvertising
// Enables advertising with the following parameters:
//     o General discoverable mode.
//     o Undirected connectable mode.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    // Set advertising data
    memset(&fields, 0, sizeof fields);

    // Flags: discoverable, BLE-only
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    // Include tx power level (filled automatically)
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // 128 bit UUID
    fields.uuids128 = &BLEGattServer::GATT_RICV2_MAIN_SERVICE_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    // Set the advertising data
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        LOG_E(MODULE_PREFIX, "error setting adv fields; rc=%d", rc);
        return false;
    }

    // Clear fields
    memset(&fields, 0, sizeof fields);

    // Set the advertising name
    if (_getAdvertisingNameFn)
    {
        if (ble_svc_gap_device_name_set(_getAdvertisingNameFn().c_str()) != 0)
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
    if (rc != 0)
    {
        LOG_E(MODULE_PREFIX, "error setting adv rsp fields; rc=%d", rc);
        return false;
    }

    // Begin advertising
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(_ownAddrType, NULL, BLE_HS_FOREVER,
                           &adv_params,
                           [](struct ble_gap_event *event, void *arg) {
                                 return ((BLEGapServer*)arg)->nimbleGapEvent(event);
                           }, 
                           this);
    if (rc != 0)
    {
        LOG_E(MODULE_PREFIX, "error enabling adv; rc=%d", rc);
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// stopAdvertising
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGapServer::stopAdvertising()
{
    if (!_isInit)
        return;
    ble_gap_adv_stop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GAP event
//
// The nimble host executes this callback when a GAP event occurs.  The
// application associates a GAP event callback with each connection that forms.
// The same callback is used for all connections.
// 
// @param event                 The type of event being signalled.
// @param ctxt                  Various information pertaining to the event.
// @param arg                   Application-specified argument; unused
// 
// @return                      0 if the application successfully handled the
//                                  event; nonzero on failure.  The semantics
//                                  of the return code is specific to the
//                                  particular GAP event being signalled.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////   
   
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
            statusStr = BLEGattServer::getHSErrorMsg(event->conn_update.status);
            connHandle = event->conn_update.conn_handle;
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
            _gattServer.getOutbound().notifyTxComplete();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            // Handle subscription to GATT attr
            _gattServer.handleSubscription(event, statusStr);
            break;
        case BLE_GAP_EVENT_MTU:
            statusStr = "mtu:" + String(event->mtu.value) + ",chanID:" + String(event->mtu.channel_id);
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            errorCode = gapEventRepeatPairing(event);
            break;
    }

#ifdef DEBUG_BLE_GAP_EVENT
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
#endif

    return errorCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BLE task - runs until nimble_port_stop
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Callback for GATT access
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
            _testPerfPrbsState = parkmiller_next(_testPerfPrbsState);
            if (payloadbuffer[i] != (_testPerfPrbsState & 0xff)) {
                isDataOk = false;
            }
        }

        // Update test stats
        _bleStats.rxTestFrame(payloadlength, isSeqOk, isDataOk);

        // Message
        LOG_I(MODULE_PREFIX, "%s inMsgCount %4d rate %8.1fBytesPS seqErrs %4d dataErrs %6d  len %4d", 
                    isFirst ? "FIRST  " : (isSeqOk & isDataOk) ? "OK     " : !isSeqOk ? "SEQERR " : "DATERR ",
                    inMsgCount,
                    _bleStats.getTestRate(),
                    _bleStats.getTestSeqErrCount(),
                    _bleStats.getTestDataErrCount(),
                    payloadlength);        
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
// Check ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Send message over BLE
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGapServer::sendBLEMsg(CommsChannelMsg& msg)
{
    if (!_isInit)
        return false;
    return _gattServer.sendMsg(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set connection state
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Start the BLE stack
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
    nimble_port_init();

    // onReset callback
    ble_hs_cfg.reset_cb = [](int reason) {
#ifdef WARN_BLE_ON_RESET_EVENT            
        LOG_I(MODULE_PREFIX, "onReset() reason=%d", reason);
#endif
    };

    // onSync callback
    ble_hs_cfg.sync_cb = []() {
        if (_pThis)
            _pThis->onSync();
    };

    ble_hs_cfg.gatts_register_cb = BLEGattServer::registrationCallbackStatic;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Not really explained here
    // https://microchipdeveloper.com/wireless:ble-gap-security
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_DISP;
    ble_hs_cfg.sm_sc = 0;

    int rc = _gattServer.start();
    if (rc == 0)
    {
        // Set the advertising name
        if (_getAdvertisingNameFn)
            rc = ble_svc_gap_device_name_set(_getAdvertisingNameFn().c_str());
    }
    else
    {
        LOG_W(MODULE_PREFIX, "nimbleStart _gattServer.initServer() failed rc=%d", rc);
    }

    // Start the host task
    nimble_port_freertos_init(bleHostTask);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop the BLE stack
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGapServer::nimbleStop()
{
    int ret = nimble_port_stop();
    if (ret != 0)
    {
        LOG_W(MODULE_PREFIX, "nimbleStop nimble_port_stop() failed ret=%d", ret);
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
// PRBS for throughput testing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t BLEGapServer::parkmiller_next(uint32_t seed) const
{
    uint32_t hi = 16807 * (seed & 0xffff);
    uint32_t lo = 16807 * (seed >> 16);
    lo += (hi & 0x7fff) << 16;
    lo += hi >> 15;
    if (lo > 0x7fffffff)
        lo -= 0x7fffffff;
    return lo;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Log information about a connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Get name of GAP event
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Handle GAP connect event
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int BLEGapServer::gapEventConnect(struct ble_gap_event *event, String& statusStr, int& connHandle)
{
    // A new connection was established or a connection attempt failed
    int rc = 0;
    if (event->connect.status == 0)
    {
        // Return values
        statusStr = "conn-ok";
        connHandle = event->connect.conn_handle;
        
        // Request preferred MTU
        rc = ble_att_set_preferred_mtu(PREFERRED_MTU_VALUE);
        if (rc != 0) 
        {
            LOG_W(MODULE_PREFIX, "nimbleGAPEvent conn failed to set preferred MTU; rc = %d", rc);
        }

        // Upgrade data length (DLE)
        rc = ble_hs_hci_util_set_data_len(event->connect.conn_handle,
                            LL_PACKET_LENGTH,
                            LL_PACKET_TIME);

        // Request an update to the connection parameters to try to ensure minimum connection interval
        struct ble_gap_upd_params params;
        memset(&params, 0, sizeof(params));
        params.itvl_min = 6;
        params.itvl_max = 6;
        params.latency = 0;
        params.supervision_timeout = 1000;
        params.min_ce_len = 0x0001;
        params.max_ce_len = 0x0001;
        rc = ble_gap_update_params(event->connect.conn_handle, &params);
        if (rc != 0)
        {
            LOG_W(MODULE_PREFIX, "nimbleGAPEvent conn failed to set update params; rc = %d", rc);
        }

        // Now connected
        setConnState(true, event->connect.conn_handle);
    }
    else
    {
        // Return values
        statusStr = "conn-fail";

        // Not connected
        setConnState(false);

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
    return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle GAP disconnect event
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int BLEGapServer::gapEventDisconnect(struct ble_gap_event *event, String& statusStr, int& connHandle)
{
    // Return values
    statusStr = "disconn reason " + BLEGattServer::getHSErrorMsg(event->disconnect.reason);
    connHandle = event->disconnect.conn.conn_handle;

    // Connection terminated
    setConnState(false);

    // Note that if USE_TIMED_ADVERTISING_CHECK is defined then
    // advertising will restart due to check in service()
#ifndef USE_TIMED_ADVERTISING_CHECK
    // Restart advertising
    startAdvertising();
#endif
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle GAP repeat pairing event
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Service restart if required
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGapServer::serviceRestartIfRequired()
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
// Service timed advertising check
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGapServer::serviceTimedAdvertisingCheck()
{
#ifdef USE_TIMED_ADVERTISING_CHECK
    // Handle advertising check
    if ((!_isConnected) && (_advertisingCheckRequired))
    {
        if (Raft::isTimeout(millis(), _advertisingCheckMs, ADVERTISING_CHECK_MS))
        {
            _advertisingCheckMs = millis();
            // Check advertising
            if (!ble_gap_adv_active())
            {
                // Debug
#ifdef WARN_ON_BLE_ADVERTISING
                LOG_W(MODULE_PREFIX, "service not conn or adv so start advertising");
#endif

                // Start advertising
                bool startAdvOk = startAdvertising();

                // Debug
#ifdef WARN_ON_BLE_ADVERTISING_START_FAILURE
                if (!startAdvOk)
                {
                    LOG_W(MODULE_PREFIX, "service started advertising FAILED");
                }
#endif
#ifdef DEBUG_BLE_ADVERTISING
                if (startAdvOk)
                {
                    LOG_W(MODULE_PREFIX, "service started advertising ok");
                }
#endif
            }
            else
            {
#ifdef DEBUG_BLE_ADVERTISING
                LOG_I(MODULE_PREFIX, "service BLE already advertising");
#endif
                _advertisingCheckRequired = false;
            }
        }
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service getting RSSI
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGapServer::serviceGettingRSSI()
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
            uint32_t rslt = ble_gap_conn_rssi(_bleGapConnHandle, &_rssi);
#ifdef DEBUG_RSSI_GET_TIME
            uint64_t endUs = micros();
            LOG_I(MODULE_PREFIX, "service get RSSI %d us", (int)(endUs - startUs));
#endif
            if (rslt != 0)
            {
                _rssi = 0;
                // Debug
                // LOG_W(MODULE_PREFIX, "service get RSSI failed");
            }
        }
    }
}

#endif // CONFIG_BT_ENABLED
