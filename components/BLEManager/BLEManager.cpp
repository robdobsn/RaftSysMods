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
#include "BLEGattServer.h"
#include <RestAPIEndpointManager.h>
#include <CommsCoreIF.h>
#include <CommsChannelMsg.h>
#include <CommsChannelSettings.h>
#include <RaftUtils.h>
#include <ESPUtils.h>
#include <SysManager.h>

#ifdef CONFIG_BT_ENABLED
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "../src/ble_hs_hci_priv.h"
#endif

// Use a separate thread to send over BLE
// #define USE_SEPARATE_THREAD_FOR_BLE_SENDING

// Send BLE message as soon as TX event occurs indicating notify
// #define SEND_BLE_MESSAGE_FROM_TX_EVENT

// Warn
#define WARN_ON_BLE_INIT_FAILED
#define WARN_ON_BLE_ADVERTISING_START_FAILURE
#define WARN_ON_ONSYNC_ADDR_ERROR

// Debug
// #define DEBUG_ONSYNC_ADDR
// #define DEBUG_BLE_ADVERTISING
// #define DEBUG_BLE_ON_RESET
// #define DEBUG_BLE_RX_PAYLOAD
// #define DEBUG_BLE_TX_MSG
// #define DEBUG_BLE_TX_MSG_SPLIT
// #define DEBUG_BLE_TX_MSG_DETAIL
// #define DEBUG_SEND_FROM_OUTBOUND_QUEUE
// #define DEBUG_BLE_PUBLISH
// #define DEBUG_BLE_SETUP
// #define DEBUG_LOG_CONNECT
// #define DEBUG_LOG_CONNECT_DETAIL
// #define DEBUG_LOG_DISCONNECT_DETAIL
// #define DEBUG_LOG_CONN_UPDATE_DETAIL
// #define DEBUG_LOG_CONN_UPDATE
// #define DEBUG_LOG_GAP_EVENT
// #define DEBUG_GAP_EVENT_DISC
// #define DEBUG_BLE_ENC_CHANGE
// #define DEBUG_BLE_ENC_CHANGE_DETAIL
// #define DEBUG_BLE_EVENT_NOTIFY_RX
// #define DEBUG_BLE_EVENT_NOTIFY_TX
// #define DEBUG_BLE_EVENT_SUBSCRIBE
// #define DEBUG_BLE_EVENT_MTU
// #define DEBUG_BLE_TASK_STARTED
// #define DEBUG_RSSI_GET_TIME

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Statics, etc
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// NOTE: this is the main service in BLEGattServer
#ifdef CONFIG_BT_ENABLED
// Log prefix
static const char *MODULE_PREFIX = "BLEMan";

ble_uuid128_t BLEManager::BLE_RICV2_ADVERTISING_UUID = BLEGattServer::GATT_RICV2_MAIN_SERVICE_UUID;
#endif

// Singleton BLEManager
BLEManager *BLEManager::_pBLEManager = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BLEManager::BLEManager(const char *pModuleName, ConfigBase &defaultConfig, ConfigBase *pGlobalConfig, 
                ConfigBase *pMutableConfig, const char* defaultAdvName)
    : SysModBase(pModuleName, defaultConfig, pGlobalConfig, pMutableConfig),
      _bleFragmentQueue(DEFAULT_OUTBOUND_MSG_QUEUE_SIZE)
{
    // BLE interface
    _enableBLE = false;
    _BLEDeviceInitialised = false;
    _defaultAdvName = defaultAdvName;
    _bleRestartState = BLERestartState_Idle;

    // ChannelID
    _commsChannelID = CommsCoreIF::CHANNEL_ID_UNDEFINED;

    // Singleton
    _pBLEManager = this;

    // Outbound msg timing
    _lastOutboundMsgMs = 0;

    // Outbound queue task
    _outboundMsgTaskHandle = nullptr;
}

BLEManager::~BLEManager()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::setup()
{
    applySetup();
}

void BLEManager::applySetup()
{
    #ifdef CONFIG_BT_ENABLED

    // See if BLE enabled
    _enableBLE = configGetBool("enable", false);
#ifdef DEBUG_BLE_SETUP
    LOG_I(MODULE_PREFIX, "applySetup BLE enabled %c", _enableBLE ? 'Y' : 'N');
#endif

    // Setup if enabled
    if (_enableBLE)
    {
        // Settings
        _maxPacketLength = configGetLong("maxPktLen", MAX_BLE_PACKET_LEN_DEFAULT);
        _outboundQueueSize = configGetLong("outQSize", DEFAULT_OUTBOUND_MSG_QUEUE_SIZE);
        _bleFragmentQueue.setMaxLen(_outboundQueueSize);

        // Start NimBLE if not already started
        if (!_BLEDeviceInitialised)
        {
            // Start NimBLE
            if (!nimbleStart())
                return;
            _BLEDeviceInitialised = true;
        }

        // Currently disconnected
        setIsConnected(false);

        // Check if a thread should be started for sending
#ifdef USE_SEPARATE_THREAD_FOR_BLE_SENDING
        UBaseType_t taskCore = configGetLong("taskCore", DEFAULT_TASK_CORE);
        BaseType_t taskPriority = configGetLong("taskPriority", DEFAULT_TASK_PRIORITY);
        int taskStackSize = configGetLong("taskStack", DEFAULT_TASK_SIZE_BYTES);

        // Start the worker task
        BaseType_t retc = pdPASS;
        if (_outboundMsgTaskHandle == nullptr)
        {
            retc = xTaskCreatePinnedToCore(
                        outboundMsgTaskStatic,
                        "BLEOutQ",                              // task name
                        taskStackSize,                          // stack size of task
                        this,                                   // parameter passed to task on execute
                        taskPriority,                           // priority
                        (TaskHandle_t*)&_outboundMsgTaskHandle, // task handle
                        taskCore);                              // pin task to core N
        }

        // Debug
        LOG_I(MODULE_PREFIX, "setup maxPktLen %d task %s(%d) core %d priority %d stack %d outQSlots %d minMsBetweenSends %d",
                    _maxPacketLength,
                    (retc == pdPASS) ? "OK" : "FAILED", retc, 
                    taskCore, taskPriority, taskStackSize,
                    _outboundQueueSize,
                    BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS);
        LOG_I(MODULE_PREFIX, "applySetup maxPktLen %d", _maxPacketLength);
#else
        // Debug
        LOG_I(MODULE_PREFIX, "setup maxPktLen %d using service loop outQSlots %d minMsBetweenSends %d",
                    _maxPacketLength,
                    _outboundQueueSize,
                    BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS);
        LOG_I(MODULE_PREFIX, "applySetup maxPktLen %d", _maxPacketLength);
#endif
    }
    else if (_BLEDeviceInitialised)
    {
#ifdef USE_SEPARATE_THREAD_FOR_BLE_SENDING
        // Shutdown task
        xTaskNotifyGive(_outboundMsgTaskHandle);
#endif

        // Deinit GATTServer
        BLEGattServer::deinitServer();

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
        _BLEDeviceInitialised = false;

        // Debug
#ifdef DEBUG_BLE_SETUP
        LOG_I(MODULE_PREFIX, "applySetup de-init ok");
#endif
    }
#endif // CONFIG_BT_ENABLED
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::service()
{
#ifdef CONFIG_BT_ENABLED

    // Check we are initialised
    if (!_BLEDeviceInitialised)
        return;

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
            return;
        case BLERestartState_StartRequired:
            if (Raft::isTimeout(millis(), _bleRestartLastMs, BLE_RESTART_BEFORE_START_MS))
            {
                // Start the BLE stack
                nimbleStart();
                _bleRestartState = BLERestartState_Idle;
                _bleRestartLastMs = millis();
            }
            return;
    }

#ifdef USE_TIMED_ADVERTISING_CHECK
    // Handle advertising check
    if ((!_isConnected) && (_advertisingCheckRequired))
    {
        if (Raft::isTimeout(millis(), _advertisingCheckMs, ADVERTISING_CHECK_MS))
        {
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
                    delay(50);
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
                LOG_I(MODULE_PREFIX, "service BLE advertising check ok");
#endif
                _advertisingCheckRequired = false;
            }
            _advertisingCheckMs = millis();
        }
    }
#endif

#ifndef USE_SEPARATE_THREAD_FOR_BLE_SENDING
    // Send if not using alternate thread
    if (!_outboundMsgInFlight)
    {
        handleSendFromOutboundQueue();
    }
    else
    {
        // Check for timeout on in flight message
        if (Raft::isTimeout(millis(), _outbountMsgInFlightStartMs, BLE_OUTBOUND_MSG_IN_FLIGHT_TIMEOUT_MS))
        {
            // Debug
            LOG_W(MODULE_PREFIX, "service outbound msg timeout");

            // Timeout so send again
            _outboundMsgInFlight = false;
            handleSendFromOutboundQueue();
        }
    }
#endif

    // Get RSSI value if connected
    // Don't set RSSI_CHECK_MS too low as getting RSSI info taked ~2ms
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
#endif // CONFIG_BT_ENABLED
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
#endif // CONFIG_BT_ENABLED
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Comms channels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::addCommsChannels(CommsCoreIF& commsCoreIF)
{
#ifdef CONFIG_BT_ENABLED

    // Comms channel
    static const CommsChannelSettings commsChannelSettings(_maxPacketLength, _maxPacketLength, 0, 0, _maxPacketLength, 0);

    // Register as a message channel
    _commsChannelID = commsCoreIF.registerChannel("RICSerial", 
            "BLE",
            "BLE",
            std::bind(&BLEManager::sendBLEMsg, this, std::placeholders::_1),
            std::bind(&BLEManager::isReadyToSend, this, std::placeholders::_1, std::placeholders::_2),
            &commsChannelSettings);
#endif // CONFIG_BT_ENABLED
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get status JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
String BLEManager::getStatusJSON()
{
#ifdef CONFIG_BT_ENABLED
    if (!_BLEDeviceInitialised)
#endif
        return R"({"rslt":"failDisabled"})";

#ifdef CONFIG_BT_ENABLED
    String rssiStr;
    if (_isConnected)
    {
        rssiStr = "\"rssi\":" + String(_rssi) + ",";
    }

    char statusStr[200];
    snprintf(statusStr, sizeof(statusStr), 
                R"({"rslt":"ok","isConn":%d,"isAdv":%d,"advName":"%s",%s"BLEMAC":"%s",)",
                _isConnected,
                ble_gap_adv_active(),
                ble_svc_gap_device_name(),
                rssiStr.c_str(),
                getSystemMACAddressStr(ESP_MAC_BT, ":").c_str()
                );

    String statusJSON = statusStr + _bleStats.getJSON(false, false) + "}";

    return statusJSON;
#endif // CONFIG_BT_ENABLED
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String BLEManager::getDebugJSON()
{
#ifdef CONFIG_BT_ENABLED
    if (!_BLEDeviceInitialised)
#endif
        return R"({"s":"none"})";

#ifdef CONFIG_BT_ENABLED
    bool advertisingActive = ble_gap_adv_active();
    bool discoveryActive = ble_gap_disc_active();
    bool gapConnActive = ble_gap_conn_active();

    String rssiStr;
    if (_isConnected)
    {
        rssiStr = "\"rssi\":" + String(_rssi) + ",";
    }

    // Also ble_gap_conn_desc can be obtained
    String advertisingInfo;
    if (advertisingActive)
        advertisingInfo = R"("adv":")" + String(ble_svc_gap_device_name()) + R"(",)";
    char tmpBuf[200];
    snprintf(tmpBuf, sizeof(tmpBuf)-1, R"({"s":"%s",%s%s)",
        _isConnected ? (gapConnActive ? "actv" : "conn") : (advertisingActive ? "adv" : (discoveryActive ? "disco" : "none")),
        rssiStr.c_str(),
        advertisingInfo.c_str());
    String statusStr = tmpBuf + _bleStats.getJSON(false, true) + "}";
    return statusStr;
#endif // CONFIG_BT_ENABLED
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GetNamedValue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double BLEManager::getNamedValue(const char* valueName, bool& isValid)
{
    switch(valueName[0])
    {
        case 'R': { isValid = true; return _rssi; }
        default: { isValid = false; return 0; }
    }
}

#ifdef CONFIG_BT_ENABLED

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// onSync callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::onSync()
{
    if (!_BLEDeviceInitialised)
        return;

    // Validate
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    // Figure out address to use while advertising (no privacy for now)
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
#ifdef WARN_ON_ONSYNC_ADDR_ERROR
        LOG_W(MODULE_PREFIX, "onSync() error determining address type; rc=%d", rc);
#endif
        return;
    }

    // Debug showing addr
    uint8_t addrVal[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addrVal, NULL);
#ifdef DEBUG_ONSYNC_ADDR
    LOG_I(MODULE_PREFIX, "onSync() Device Address: %x:%x:%x:%x:%x:%x",
              addrVal[5], addrVal[4], addrVal[3], addrVal[2], addrVal[1], addrVal[0]);
#endif

    // Start advertising
    if (!startAdvertising())
    {
#ifdef WARN_ON_BLE_ADVERTISING_START_FAILURE
        LOG_W(MODULE_PREFIX, "onSync started advertising FAILED");
        delay(50);
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// startAdvertising
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
bool BLEManager::startAdvertising()
{
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

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 128 bit UUID
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    fields.uuids128 = &BLE_RICV2_ADVERTISING_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        LOG_E(MODULE_PREFIX, "error setting adv fields; rc=%d", rc);
        return false;
    }

    // Clear fields
    memset(&fields, 0, sizeof fields);

    // Set the advertising name
    if (ble_svc_gap_device_name_set(_configuredAdvertisingName.c_str()) != 0)
    {
        LOG_E(MODULE_PREFIX, "error setting adv name rc=%d", rc);
    }

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0)
    {
        LOG_E(MODULE_PREFIX, "error setting adv rsp fields; rc=%d", rc);
        return false;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, nimbleGapEventStatic, NULL);
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

void BLEManager::stopAdvertising()
{
    ble_gap_adv_stop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Log information about a connection to the console.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::logConnectionInfo(struct ble_gap_conn_desc *desc)
{
    LOG_I(MODULE_PREFIX, "handle=%d our_ota_addr_type=%d our_ota_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                desc->conn_handle, desc->our_ota_addr.type, 
                desc->our_ota_addr.val[0],
				desc->our_ota_addr.val[1],
				desc->our_ota_addr.val[2],
				desc->our_ota_addr.val[3],
				desc->our_ota_addr.val[4],
				desc->our_ota_addr.val[5]);
    LOG_I(MODULE_PREFIX, " our_id_addr_type=%d our_id_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                desc->our_id_addr.type, 
                desc->our_id_addr.val[0],
				desc->our_id_addr.val[1],
				desc->our_id_addr.val[2],
				desc->our_id_addr.val[3],
				desc->our_id_addr.val[4],
				desc->our_id_addr.val[5]);
    LOG_I(MODULE_PREFIX, " peer_ota_addr_type=%d peer_ota_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                desc->peer_ota_addr.type, 
                desc->peer_ota_addr.val[0],
				desc->peer_ota_addr.val[1],
				desc->peer_ota_addr.val[2],
				desc->peer_ota_addr.val[3],
				desc->peer_ota_addr.val[4],
				desc->peer_ota_addr.val[5]);
    LOG_I(MODULE_PREFIX, " peer_id_addr_type=%d peer_id_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                desc->peer_id_addr.type, 
                desc->peer_id_addr.val[0],
				desc->peer_id_addr.val[1],
				desc->peer_id_addr.val[2],
				desc->peer_id_addr.val[3],
				desc->peer_id_addr.val[4],
				desc->peer_id_addr.val[5]);
    LOG_I(MODULE_PREFIX, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                      "encrypted=%d authenticated=%d bonded=%d",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
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
   
int BLEManager::nimbleGapEventStatic(struct ble_gap_event *event, void *arg)
{
    // LOG_D(MODULE_PREFIX, "GattAccessCallback %s %s len %d\n", characteristicName, readOp ? "read" : "write", payloadlength);
    if (_pBLEManager)
        return _pBLEManager->nimbleGapEvent(event, arg);
    return 0;
}

int BLEManager::nimbleGapEvent(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;

    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
        {
            // A new connection was established or a connection attempt failed
#ifdef DEBUG_LOG_CONNECT
            LOG_I(MODULE_PREFIX, "GAPEvent conn %s (%d) ",
                        event->connect.status == 0 ? "established" : "failed",
                        event->connect.status);
#endif
            if (event->connect.status == 0)
            {
                int rc = ble_att_set_preferred_mtu(PREFERRED_MTU_VALUE);
                if (rc != 0) {
                    LOG_W(MODULE_PREFIX, "nimbleGAPEvent conn failed to set preferred MTU; rc = %d", rc);
                }
                rc = ble_hs_hci_util_set_data_len(event->connect.conn_handle,
                                    LL_PACKET_LENGTH,
                                    LL_PACKET_TIME);
                setIsConnected(true, event->connect.conn_handle);
                rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
                assert(rc == 0);
#ifdef DEBUG_LOG_CONNECT_DETAIL
                logConnectionInfo(&desc);
#endif

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
            }
            else
            {
                setIsConnected(false);
                // Connection failed; resume advertising
                if (!startAdvertising())
                {
#ifdef WARN_ON_BLE_ADVERTISING_START_FAILURE
                    LOG_W(MODULE_PREFIX, "nimbleGAPEvent conn start advertising FAILED");
                    delay(50);
#endif
                }
                else
                {
                    LOG_I(MODULE_PREFIX, "GAPEvent conn resumed advertising after connection failure");
                }
            }
            return 0;
        }
        case BLE_GAP_EVENT_DISCONNECT:
        {
            LOG_W(MODULE_PREFIX, "nimbleGapEvent disconnect; reason=%d ", event->disconnect.reason);
            setIsConnected(false);
#ifdef DEBUG_LOG_DISCONNECT_DETAIL
            logConnectionInfo(&event->disconnect.conn);
#endif

            // Connection terminated; resume advertising
            // LOG_W(MODULE_PREFIX, "GAPEvent disconnect - requesting startAdvertising");
            // startAdvertising();
            return 0;
        }
        case BLE_GAP_EVENT_CONN_UPDATE:
        { 
            // The central has updated the connection parameters
#ifdef DEBUG_LOG_CONN_UPDATE
            LOG_I(MODULE_PREFIX, "nimbleGAPEvent connection updated; status=%d ",
                        event->conn_update.status);
#endif
            int rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
            assert(rc == 0);
#ifdef DEBUG_LOG_CONN_UPDATE_DETAIL
            logConnectionInfo(&desc);
#endif
            return 0;
        }
        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        {
#ifdef DEBUG_LOG_CONN_UPDATE
            LOG_I(MODULE_PREFIX, "nimbleGAPEvent connection update request");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        {
#ifdef DEBUG_LOG_CONN_UPDATE
            LOG_I(MODULE_PREFIX, "nimbleGAPEvent L2CAP update request");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_TERM_FAILURE:
        {
#ifdef DEBUG_LOG_GAP_EVENT
            LOG_I(MODULE_PREFIX, "GAPEvent term failure");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_DISC:
        {
#ifdef DEBUG_GAP_EVENT_DISC
            LOG_I(MODULE_PREFIX, "GAPEvent DISC");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
        {
#ifdef DEBUG_GAP_EVENT_DISC
            LOG_I(MODULE_PREFIX, "GAPEvent DISC COMPLETE");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_ADV_COMPLETE:
        {
#ifdef DEBUG_BLE_ADVERTISING
            LOG_I(MODULE_PREFIX, "GAPEvent advertise complete; reason=%d",
                        event->adv_complete.reason);
#endif
            // Connection failed; resume advertising
            if (!startAdvertising())
            {
#ifdef WARN_ON_BLE_ADVERTISING_START_FAILURE
                    LOG_W(MODULE_PREFIX, "nimbleGapEvent started advertising FAILED");
                    delay(50);
#endif
            }
            return 0;
        }
        case BLE_GAP_EVENT_ENC_CHANGE:
        {
            // Encryption has been enabled or disabled for this connection
#ifdef DEBUG_BLE_ENC_CHANGE
            LOG_I(MODULE_PREFIX, "GAPEvent encryption change; status=%d ",
                        event->enc_change.status);
#endif
            int rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            assert(rc == 0);
#ifdef DEBUG_BLE_ENC_CHANGE_DETAIL
            logConnectionInfo(&desc);
#endif
            return 0;
        }
        case BLE_GAP_EVENT_PASSKEY_ACTION:
        {
#ifdef DEBUG_BLE_ENC_CHANGE_DETAIL
            LOG_I(MODULE_PREFIX, "GAPEvent PASSKEY action");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_NOTIFY_RX:
        {
#ifdef DEBUG_BLE_EVENT_NOTIFY_RX
            LOG_I(MODULE_PREFIX, "GAPEvent notify RX");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_NOTIFY_TX:
        {
#ifdef DEBUG_BLE_EVENT_NOTIFY_TX
            LOG_I(MODULE_PREFIX, "GAPEvent notify TX");
#endif
            // No message in flight
            _outboundMsgInFlight = false;

#ifdef SEND_BLE_MESSAGE_FROM_TX_EVENT
            // Check if more messages to send immediately
            handleSendFromOutboundQueue();
#endif
            return 0;
        }
        case BLE_GAP_EVENT_SUBSCRIBE:
        {
#ifdef DEBUG_BLE_EVENT_SUBSCRIBE
            LOG_I(MODULE_PREFIX, "GAPEvent subscribe conn_handle=%d attr_handle=%d "
                            "reason=%d prevn=%d curn=%d previ=%d curi=%d",
                        event->subscribe.conn_handle,
                        event->subscribe.attr_handle,
                        event->subscribe.reason,
                        event->subscribe.prev_notify,
                        event->subscribe.cur_notify,
                        event->subscribe.prev_indicate,
                        event->subscribe.cur_indicate);
#endif
            // Handle subscription to GATT attr
            BLEGattServer::handleSubscription(event, _bleGapConnHandle);
            return 0;
        }
        case BLE_GAP_EVENT_MTU:
        {
#ifdef DEBUG_BLE_EVENT_MTU
            LOG_I(MODULE_PREFIX, "mtu update event; conn_handle=%d cid=%d mtu=%d",
                        event->mtu.conn_handle,
                        event->mtu.channel_id,
                        event->mtu.value);
#endif
            return 0;
        }
        case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        {
#ifdef DEBUG_LOG_GAP_EVENT
            LOG_I(MODULE_PREFIX, "GAPEvent identity resolved");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_REPEAT_PAIRING:
        { 
#ifdef DEBUG_LOG_GAP_EVENT
            LOG_I(MODULE_PREFIX, "GAPEvent Repeat Pairing");
#endif
            /* We already have a bond with the peer, but it is attempting to
            * establish a new secure link.  This app sacrifices security for
            * convenience: just throw away the old bond and accept the new link.
            */

            // Delete the old bond
            int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            assert(rc == 0);
            ble_store_util_delete_peer(&desc.peer_id_addr);

            // Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
            // continue with the pairing operation.
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        {
#ifdef DEBUG_LOG_GAP_EVENT
            LOG_I(MODULE_PREFIX, "GAPEvent PHY update complete");
#endif
            return 0;
        }
        case BLE_GAP_EVENT_EXT_DISC:
        {
#ifdef DEBUG_GAP_EVENT_DISC
            LOG_I(MODULE_PREFIX, "GAPEvent EXT DISC");
#endif
            return 0;
        }
    }

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BLE task - runs until nimble_port_stop
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::bleHostTask(void *param)
{
    // This function will return only when nimble_port_stop() is executed
#ifdef DEBUG_BLE_TASK_STARTED
    LOG_I(MODULE_PREFIX, "BLE Host Task Started");
#endif
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callback for GATT access
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::gattAccessCallbackStatic(const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength)
{
    // LOG_D(MODULE_PREFIX, "GattAccessCallback %s %s len %d\n", characteristicName, readOp ? "read" : "write", payloadlength);
    if (_pBLEManager)
        _pBLEManager->gattAccessCallback(characteristicName, readOp, payloadbuffer, payloadlength);
}

void BLEManager::gattAccessCallback(const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength)
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
        if (getCommsCore())
            getCommsCore()->handleInboundMessage(_commsChannelID, payloadbuffer, payloadlength);

#ifdef DEBUG_BLE_RX_PAYLOAD
        // Debug
        uint32_t sz = payloadlength;
        const uint8_t* pVals = payloadbuffer;
        char outBuf[400];
        strcpy(outBuf, "");
        char tmpBuf[10];
        for (int i = 0; i < sz; i++)
        {
            sprintf(tmpBuf, "%02x ", pVals[i]);
            strlcat(outBuf, tmpBuf, sizeof(outBuf));
        }
        LOG_I(MODULE_PREFIX, "gatt rx payloadLen %d payload %s", sz, outBuf);
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if ready to send message over BLE
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEManager::isReadyToSend(uint32_t channelID, bool& noConn)
{
    // Check for connection
    noConn = false;
    if (!_isConnected)
    {
        noConn = true;
        return false;
    }
    // Check state of gatt server
    noConn = !BLEGattServer::isNotificationEnabled();
    if (noConn)
        return false;
    // Check the queue is empty
    if (_outboundMsgInFlight || (_bleFragmentQueue.count() > 0))
        return false;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message over BLE
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEManager::sendBLEMsg(CommsChannelMsg& msg)
{
    if (!_BLEDeviceInitialised)
        return false;

#ifdef DEBUG_BLE_TX_MSG
#ifndef DEBUG_BLE_PUBLISH
    if (msg.getMsgTypeCode() != MSG_TYPE_PUBLISH) 
    {
#endif
        LOG_I(MODULE_PREFIX, "sendBLEMsg channelID %d, msgType %s msgNum %d, len %d msg %s",
            msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen(), msg.getBuf());
#ifndef DEBUG_BLE_PUBLISH
    }
#endif
#endif

    // Check if publish message and only add if outbound queue is empty
    if ((msg.getMsgTypeCode() == MSG_TYPE_PUBLISH) && (_bleFragmentQueue.count() != 0))
        return false;

    // Split into multiple messages if required
    const uint8_t* pMsgPtr = msg.getBuf();
    uint32_t remainingLen = msg.getBufLen();
    for (int msgIdx = 0; msgIdx < _outboundQueueSize; msgIdx++)
    {
        uint32_t msgLen = _maxPacketLength;
        if (msgLen > remainingLen)
            msgLen = remainingLen;

        // Send to the queue
        ProtocolRawMsg bleOutMsg(pMsgPtr, msgLen);
        bool putOk = _bleFragmentQueue.put(bleOutMsg);

#ifdef DEBUG_BLE_TX_MSG_SPLIT
        if (msg.getBufLen() > _maxPacketLength) {
            const char* pDebugHex = nullptr;
#ifdef DEBUG_BLE_TX_MSG_DETAIL
            String hexStr;
            Raft::getHexStrFromBytes(pMsgPtr, msgLen, hexStr);
            pDebugHex = hexStr.c_str();
#endif
            LOG_W(MODULE_PREFIX, "sendBLEMsg msgIdx %d partOffset %d partLen %d totalLen %d remainLen %d putOk %d %s", 
                    msgIdx, pMsgPtr-msg.getBuf(), msgLen, msg.getBufLen(), remainingLen, putOk,
                    pDebugHex ? pDebugHex : "");
        }
#endif
        // Check ok
        if (!putOk)
            break;

        // Calculate remaining
        remainingLen -= msgLen;
        pMsgPtr += msgLen;
        if (remainingLen == 0)
            break;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set connection state
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::setIsConnected(bool isConnected, uint16_t connHandle)
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
    BLEGattServer::setIsConnected(isConnected, connHandle);
    
    // Inform hooks of status change
    if (_pBLEManager)
        _pBLEManager->executeStatusChangeCBs(isConnected);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get advertising name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String BLEManager::getAdvertisingName()
{
    // Check singleton ok
    if (!_pBLEManager)
        return "";

    // Name
    String adName = _pBLEManager->configGetString("adName", "");
    bool friendlyNameIsSet = false;
    if (adName.length() == 0)
    {
        String friendlyName = _pBLEManager->getFriendlyName(friendlyNameIsSet);
        if (friendlyNameIsSet)
            adName = friendlyName;
    }
    if ((adName.length() == 0) && (_pBLEManager))
        adName = _pBLEManager->_defaultAdvName;
    if (adName.length() == 0)
        adName = _pBLEManager->getSystemName();
    return adName;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle sending from outbound queue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::handleSendFromOutboundQueue()
{
    // Handle outbound message queue
    if (_bleFragmentQueue.count() > 0)
    {
        // if (Raft::isTimeout(millis(), _lastOutboundMsgMs, BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS))
        // {
            ProtocolRawMsg bleOutMsg;
            if (_bleFragmentQueue.get(bleOutMsg))
            {
                _outbountMsgInFlightStartMs = millis();
                _outboundMsgInFlight = true;
                bool rslt = BLEGattServer::sendToCentral(bleOutMsg.getBuf(), bleOutMsg.getBufLen());
                if (!rslt)
                {
                    _outboundMsgInFlight = false;
                }
#ifdef DEBUG_SEND_FROM_OUTBOUND_QUEUE
                LOG_I(MODULE_PREFIX, "handleSendFromOutboundQueue len %d sendOk %d inFlight %d leftInQueue %d", 
                            bleOutMsg.getBufLen(), rslt, _outboundMsgInFlight, _bleFragmentQueue.count());
#endif
                _bleStats.txMsg(bleOutMsg.getBufLen(), rslt);
            }
            _lastOutboundMsgMs = millis();
        // }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Task worker for outbound messages
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEManager::outboundMsgTaskStatic(void* pvParameters)
{
    // Get the object that requested the task
    BLEManager* pObjPtr = (BLEManager*)pvParameters;
    if (pObjPtr)
        pObjPtr->outboundMsgTask();
}

void BLEManager::outboundMsgTask()
{
    // Run the task until asked to stop
    while (ulTaskNotifyTake(pdTRUE, 0) == 0)
    {        
        // Handle queue
        handleSendFromOutboundQueue();

        // Yield
        vTaskDelay(1);
    }

    // Task has exited
    _outboundMsgTaskHandle = nullptr;
    vTaskDelete(NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API Restart BLE
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode BLEManager::apiBLERestart(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Stop advertising
    stopAdvertising();

    // Set state to stop required
    _bleRestartState = BLERestartState_StopRequired;
    _bleRestartLastMs = millis();

    // Restart in progress
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start the BLE stack
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEManager::nimbleStart()
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

    // Get NimBLE log level 
    String nimLogLev = configGetString("nimLogLev", "");
    setModuleLogLevel("NimBLE", nimLogLev);

    // onReset callback
    ble_hs_cfg.reset_cb = [](int reason) {
#ifdef DEBUG_BLE_ON_RESET            
        LOG_I(MODULE_PREFIX, "onReset() reason=%d", reason);
#endif
    };

    // onSync callback
    ble_hs_cfg.sync_cb = []() {
        if (_pBLEManager)
            _pBLEManager->onSync();
    };

    ble_hs_cfg.gatts_register_cb = BLEGattServer::registrationCallback;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Not really explained here
    // https://microchipdeveloper.com/wireless:ble-gap-security
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_DISP;
    ble_hs_cfg.sm_sc = 0;

    int rc = BLEGattServer::initServer();
    if (rc == 0)
    {
        // Set the advertising name
        _configuredAdvertisingName = getAdvertisingName();
        rc = ble_svc_gap_device_name_set(_configuredAdvertisingName.c_str());
     
        // Set the callback
        BLEGattServer::setServerAccessCB(gattAccessCallbackStatic);
    }
    else
    {
        LOG_W(MODULE_PREFIX, "nimbleStart BLEGattServer::initServer() failed rc=%d", rc);
    }

    // Start the host task
    nimble_port_freertos_init(bleHostTask);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop the BLE stack
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEManager::nimbleStop()
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

uint32_t BLEManager::parkmiller_next(uint32_t seed) const
{
    uint32_t hi = 16807 * (seed & 0xffff);
    uint32_t lo = 16807 * (seed >> 16);
    lo += (hi & 0x7fff) << 16;
    lo += hi >> 15;
    if (lo > 0x7fffffff)
        lo -= 0x7fffffff;
    return lo;
}
#endif // CONFIG_BT_ENABLED