/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGattServer
// Handles BLE GATT
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <sdkconfig.h>
#ifdef CONFIG_BT_ENABLED

#include <stdio.h>
#include <string.h>
#include <Logger.h>
#include <RaftUtils.h>
#include <ArduinoOrAlt.h>
#include "BLEGattServer.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Warn
#define WARN_ON_BLE_CHAR_WRITE_FAIL
#define WARN_ON_BLE_CHAR_READ_UNEXPECTED
#define WARN_ON_BLE_CHAR_WRITE_UNEXPECTED
#define WARN_ON_BLE_CHAR_WRITE_TAKING_TOO_LONG

// Debug
// #define DEBUG_CMD_CHARACTERISTIC
// #define DEBUG_RESP_CHARACTERISTIC
// #define DEBUG_RESP_SUBSCRIPTION
// #define DEBUG_BLE_REG_SERVICES
// #define DEBUG_BLE_REG_CHARACTERISTIC
// #define DEBUG_BLE_REG_DESCRIPTOR
// #define DEBUG_BLE_GATT_TRY_AGAIN

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Statics, etc
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Log prefix
static const char *MODULE_PREFIX = "BLEGattServer";

/**
 * The service consists of the following characteristics:
 *     o motionCommand: used to request motion
 *     o motionStatus: used to get motion status
 */

// aa76677e-9cfd-4626-a510-0d305be57c8d
ble_uuid128_t BLEGattServer::GATT_RICV2_MAIN_SERVICE_UUID =
    {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0x8d, 0x7c, 0xe5, 0x5b, 0x30, 0x0d, 0x10, 0xa5,
                  0x26, 0x46, 0xfd, 0x9c, 0x7e, 0x67, 0x76, 0xaa}};

// aa76677e-9cfd-4626-a510-0d305be57c8e
ble_uuid128_t BLEGattServer::GATT_RICV2_MESSAGE_COMMAND_UUID =
    {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0x8e, 0x7c, 0xe5, 0x5b, 0x30, 0x0d, 0x10, 0xa5,
                  0x26, 0x46, 0xfd, 0x9c, 0x7e, 0x67, 0x76, 0xaa}};

// aa76677e-9cfd-4626-a510-0d305be57c8f
ble_uuid128_t BLEGattServer::GATT_RICV2_MESSAGE_RESPONSE_UUID =
    {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0x8f, 0x7c, 0xe5, 0x5b, 0x30, 0x0d, 0x10, 0xa5,
                  0x26, 0x46, 0xfd, 0x9c, 0x7e, 0x67, 0x76, 0xaa}};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BLEGattServer::BLEGattServer(BLEGattServerAccessCBType callback, BLEManStats& bleStats) :
    _bleOutbound(*this, bleStats)
{
    _accessCallback = callback;
}

BLEGattServer::~BLEGattServer()
{
    stop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattServer::setup(uint32_t maxPacketLen, uint32_t outboundQueueSize, bool useTaskForSending,
                UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize, bool sendUsingIndication)
{
    _sendUsingIndication = sendUsingIndication;
    return _bleOutbound.setup(maxPacketLen, outboundQueueSize, 
                useTaskForSending, taskCore, taskPriority, taskStackSize,
                sendUsingIndication);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattServer::service()
{
    // Service outbound queue
    _bleOutbound.service();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattServer::isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
{
    // Check state of gatt server
    noConn = !isNotificationEnabled();
    if (noConn)
        return false;
    return _bleOutbound.isReadyToSend(channelID, msgType, noConn);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message over BLE
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BLEGattServer::sendMsg(CommsChannelMsg& msg)
{
    return _bleOutbound.sendMsg(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get data written (to characteristic) by central
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int BLEGattServer::getDataWrittenToCharacteristic(struct os_mbuf *om, std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& rxMsg)
{
    uint16_t om_len = OS_MBUF_PKTLEN(om);
    if (om_len == 0)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    rxMsg.resize(om_len);
    uint16_t len = 0;
    int rc = ble_hs_mbuf_to_flat(om, rxMsg.data(), rxMsg.size(), &len);
    return (rc == 0) ? 0 : BLE_ATT_ERR_UNLIKELY;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command Characteristic access callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int BLEGattServer::commandCharAccess(uint16_t conn_handle, uint16_t attr_handle,
                                          struct ble_gatt_access_ctxt *ctxt,
                                          void *arg)
{
#ifdef DEBUG_CMD_CHARACTERISTIC
    LOG_W(MODULE_PREFIX, "cmd char access");
#endif

    switch (ctxt->op)
    {
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
        {
            // Get the written data
            std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> rxMsg;
            int nimbleRetCode = getDataWrittenToCharacteristic(ctxt->om, rxMsg);

            // Debug
            if (nimbleRetCode == 0)
            {
#ifdef DEBUG_CMD_CHARACTERISTIC
                LOG_W(MODULE_PREFIX, "cmdCharCB opWrite rxFromCentral nimbleRetCode %d len %d", nimbleRetCode, rxMsg.size());
#endif
            }
            else
            {
#ifdef WARN_ON_BLE_CHAR_WRITE_FAIL
                LOG_W(MODULE_PREFIX, "cmdCharCB opWrite rxFromCentral failed to get mbuf nimbleRetCode=%d", nimbleRetCode);
#endif
            }

            // Callback with data
            if (_accessCallback && (nimbleRetCode==0) && (rxMsg.size() > 0))
                _accessCallback("cmdmsg", false, rxMsg);

            return nimbleRetCode;
        }
        case BLE_GATT_ACCESS_OP_READ_CHR:
        {
            // This is not expected to happen as the central is not expected
            // to read from this characteristic
#ifdef WARN_ON_BLE_CHAR_READ_UNEXPECTED
            LOG_W(MODULE_PREFIX, "cmdCharCB unexpected opRead");
#endif
            return BLE_ATT_ERR_UNLIKELY;
        }
        default:
        {
            return BLE_ATT_ERR_UNLIKELY;
        }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command Characteristic access callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int BLEGattServer::responseCharAccess(uint16_t conn_handle, uint16_t attr_handle,
                                          struct ble_gatt_access_ctxt *ctxt,
                                          void *arg)
{
#ifdef DEBUG_RESP_CHARACTERISTIC
    LOG_W(MODULE_PREFIX, "respCharCB");
#endif

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
    {
        // This is not expected to happen as the central does not sent data
        // on this characteristic
#ifdef WARN_ON_BLE_CHAR_WRITE_UNEXPECTED
        LOG_W(MODULE_PREFIX, "respCharCB unexpected opWrite");
#endif
        return 0;
    }
    case BLE_GATT_ACCESS_OP_READ_CHR:
    {
        // This is not expected to happen as the central receives data via
        // a subscription and isn't expected to read from the characteristic directly
#ifdef WARN_ON_BLE_CHAR_READ_UNEXPECTED
        char buf[BLE_UUID_STR_LEN];
        LOG_W(MODULE_PREFIX, "respCharCB unexpected opRead om %p om_len %d uuid %s", 
                    ctxt->om, ctxt->om->om_len, 
                    ble_uuid_to_str(ctxt->chr->uuid, buf));
#endif
        return 0;
    }
    default:
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Registration callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattServer::registrationCallbackStatic(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
    {
#ifdef DEBUG_BLE_REG_SERVICES
        char buf[BLE_UUID_STR_LEN];
        LOG_W(MODULE_PREFIX, "registered service %s with handle=%d",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
#endif
        break;
    }
    case BLE_GATT_REGISTER_OP_CHR:
    {
#ifdef DEBUG_BLE_REG_CHARACTERISTIC
        char buf[BLE_UUID_STR_LEN];
        LOG_W(MODULE_PREFIX, "registering characteristic %s with "
                           "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
#endif
        break;
    }
    case BLE_GATT_REGISTER_OP_DSC:
    {
#ifdef DEBUG_BLE_REG_DESCRIPTOR
        char buf[BLE_UUID_STR_LEN];
        LOG_W(MODULE_PREFIX, "registering descriptor %s with handle=%d",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
#endif
        break;
    }
    default:
    {
        break;
    }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message to central
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#define ble_gatts_indicate_custom ble_gattc_indicate_custom
#define ble_gatts_notify_custom ble_gattc_notify_custom
#endif

BLEGattServerSendResult BLEGattServer::sendToCentral(const uint8_t* pBuf, uint32_t bufLen)
{
    // Check connected
    if (!BLEGattServer::_bleIsConnected)
    {
        LOG_W(MODULE_PREFIX, "sendToCentral failed as not connected");
        return BLEGATT_SERVER_SEND_RESULT_FAIL;
    }

    // Check if we are in notify state
    if (!BLEGattServer::_responseNotifyState) 
    {
        LOG_W(MODULE_PREFIX, "sendToCentral failed as client has not subscribed");
        return BLEGATT_SERVER_SEND_RESULT_FAIL;
    }

    // Form buffer to send
    struct os_mbuf *om = ble_hs_mbuf_from_flat(pBuf, bufLen);
#ifdef DEBUG_RESP_CHARACTERISTIC
    LOG_I(MODULE_PREFIX, "sendToCentral sending bufLen %d om %p", bufLen, om);
#endif
#ifdef WARN_ON_BLE_CHAR_WRITE_TAKING_TOO_LONG
    uint64_t nowUS = micros();
#endif

    // Send
    int rc = 0;
    if (_sendUsingIndication)
    {
        rc = ble_gatts_indicate_custom(BLEGattServer::_bleGapConnHandle, BLEGattServer::_characteristicValueAttribHandle, om);
    }
    else
    {
        rc = ble_gatts_notify_custom(BLEGattServer::_bleGapConnHandle, BLEGattServer::_characteristicValueAttribHandle, om);
    }

#ifdef WARN_ON_BLE_CHAR_WRITE_TAKING_TOO_LONG
    uint64_t elapsedUs = micros() - nowUS;
    if (elapsedUs > 50000)
    {
        LOG_W(MODULE_PREFIX, "sendToCentral SLOW took %llduS", elapsedUs);
    }
#endif
    if (rc == 0)
        return BLEGATT_SERVER_SEND_RESULT_OK;
    if ((rc == BLE_HS_EAGAIN) || (rc == BLE_HS_ENOMEM))
    {
#ifdef DEBUG_BLE_GATT_TRY_AGAIN
        LOG_I(MODULE_PREFIX, "sendToCentral failed %s (%d) bufLen %d", getHSErrorMsg(rc).c_str(), rc, bufLen);
#endif
        return BLEGATT_SERVER_SEND_RESULT_TRY_AGAIN;
    }

    // Send failure
    if (Raft::isTimeout(millis(), _lastBLEErrorMsgMs, MIN_TIME_BETWEEN_ERROR_MSGS_MS) || 
                (_lastBLEErrorMsgCode != rc))
    {
        LOG_W(MODULE_PREFIX, "sendToCentral failed %s (%d) bufLen %d", getHSErrorMsg(rc).c_str(), rc, bufLen);
        _lastBLEErrorMsgCode = rc;
        _lastBLEErrorMsgMs = millis();
    }
    return BLEGATT_SERVER_SEND_RESULT_FAIL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start server
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int BLEGattServer::start()
{
    // Characteristics (zero all entries initially, the last entry must remain all zeros)
    mainServiceCharList.resize(3);
    memset(mainServiceCharList.data(), 0, sizeof(struct ble_gatt_chr_def) * mainServiceCharList.size());

    // Command characteristic
    mainServiceCharList[0].uuid = &GATT_RICV2_MESSAGE_COMMAND_UUID.u;
    mainServiceCharList[0].access_cb = [](uint16_t conn_handle, uint16_t attr_handle,
                                              struct ble_gatt_access_ctxt *ctxt,
                                              void *arg)
    {
        if (arg)
            ((BLEGattServer*)arg)->commandCharAccess(conn_handle, attr_handle, ctxt, arg);
        return 0;
    };
    mainServiceCharList[0].arg = this;
    mainServiceCharList[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;

    // Response characteristic
    mainServiceCharList[1].uuid = &GATT_RICV2_MESSAGE_RESPONSE_UUID.u;
    mainServiceCharList[1].access_cb = [](uint16_t conn_handle, uint16_t attr_handle,
                                              struct ble_gatt_access_ctxt *ctxt,
                                              void *arg)
    {
        if (arg)
            ((BLEGattServer*)arg)->responseCharAccess(conn_handle, attr_handle, ctxt, arg);
        return 0;
    };
    mainServiceCharList[1].arg = this;
    mainServiceCharList[1].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE;

    // Services list (zero all entries initially, the last entry must remain all zeros)
    servicesList.resize(2);
    memset(servicesList.data(), 0, sizeof(struct ble_gatt_svc_def) * servicesList.size());
    
    // Main service
    servicesList[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    servicesList[0].uuid = &GATT_RICV2_MAIN_SERVICE_UUID.u;
    servicesList[0].characteristics = mainServiceCharList.data();

    // Initialise GAP and GATT
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Prepare for services to be added
    int rc = ble_gatts_count_cfg(servicesList.data());
    if (rc != 0)
        return rc;

    // Add services
    rc = ble_gatts_add_svcs(servicesList.data());
    if (rc != 0)
        return rc;

    // Register 

#ifdef DEBUG_FOR_ESP32_MINI_BOARDS
    gpio_pad_select_gpio(LED_OUTPUT_TEST);
    gpio_set_direction(LED_OUTPUT_TEST, GPIO_MODE_OUTPUT);
#endif

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop server
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattServer::stop()
{
    // Stop outbound handler
    getOutbound().stop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle subscription
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BLEGattServer::handleSubscription(struct ble_gap_event * pEvent, String& statusStr)
{
    if (pEvent->subscribe.attr_handle == _characteristicValueAttribHandle) {
        _responseNotifyState = pEvent->subscribe.cur_notify != 0;
        // debug_test_nofify_reset();
    } else if (pEvent->subscribe.attr_handle != _characteristicValueAttribHandle) {
        _responseNotifyState = pEvent->subscribe.cur_notify != 0;
        // debug_test_notify_stop();
    }
    statusStr = "subscribe attr_handle=" + String(pEvent->subscribe.attr_handle) + 
                " reason=" + getHSErrorMsg(pEvent->subscribe.reason) +
                " prevNotify=" + String(pEvent->subscribe.prev_notify) +
                " curNotify=" + String(pEvent->subscribe.cur_notify) +
                " prevInd=" + String(pEvent->subscribe.prev_indicate) +
                " curInd=" + String(pEvent->subscribe.cur_indicate);
#ifdef DEBUG_RESP_SUBSCRIPTION
    LOG_W(MODULE_PREFIX, "handleSubscription notify enabled %s", _responseNotifyState ? "YES" : "NO");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get HS error message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String BLEGattServer::getHSErrorMsg(int errorCode)
{
    switch(errorCode)
    {
        case 0: return "OK";
        case BLE_HS_EAGAIN: return "TryAgain";
        case BLE_HS_EALREADY: return "AlreadyInProgress";
        case BLE_HS_EINVAL: return "InvalidArgs";
        case BLE_HS_EMSGSIZE: return "BufferTooSmall";
        case BLE_HS_ENOENT: return "NoEntry";
        case BLE_HS_ENOMEM: return "NoMem";
        case BLE_HS_ENOTCONN: return "NotConn";
        case BLE_HS_ENOTSUP: return "NotSupp";
        case BLE_HS_EAPP: return "AppCallbackErr";
        case BLE_HS_EBADDATA: return "InvalidCmd";
        case BLE_HS_EOS: return "OSerr";
        case BLE_HS_ECONTROLLER: return "ControllerErr";
        case BLE_HS_ETIMEOUT: return "Timeout";
        case BLE_HS_EDONE: return "Done";
        case BLE_HS_EBUSY: return "Busy";
        case BLE_HS_EREJECT: return "Reject";
        case BLE_HS_EUNKNOWN: return "Unknown";
        case BLE_HS_EROLE: return "Role";
        case BLE_HS_ETIMEOUT_HCI: return "TimeoutHCI";
        case BLE_HS_ENOMEM_EVT: return "NoMemEvt";
        case BLE_HS_ENOADDR: return "NoAddr";
        case BLE_HS_ENOTSYNCED: return "NotSynced";
        case BLE_HS_EAUTHEN: return "Authen";
        case BLE_HS_EAUTHOR: return "Author";
        case BLE_HS_EENCRYPT: return "Encrypt";
        case BLE_HS_EENCRYPT_KEY_SZ: return "EncryptKeySz";
        case BLE_HS_ESTORE_CAP: return "StoreCap";
        case BLE_HS_ESTORE_FAIL: return "StoreFail";
        default: return "Unknown (" + String(errorCode) + ")";
    }
}

#endif // CONFIG_BT_ENABLED
