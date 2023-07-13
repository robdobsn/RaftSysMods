/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGattServer
// Handles BLE GATT
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <sdkconfig.h>

#ifdef CONFIG_BT_ENABLED

#include <functional>
#undef min
#undef max
#include <host/ble_hs.h>
#undef min
#undef max

typedef std::function<void (const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength)> BLEGattServerAccessCBType;

class BLEGattServer
{
public:
    // UUIDs
    static ble_uuid128_t GATT_RICV2_MAIN_SERVICE_UUID;
    static ble_uuid128_t GATT_RICV2_MESSAGE_COMMAND_UUID;
    static ble_uuid128_t GATT_RICV2_MESSAGE_RESPONSE_UUID;

    // Init and deinit
    static int initServer();
    static void deinitServer();

    // Set connection handle
    static void setIsConnected(bool isConnected, uint16_t connHandle)
    {
        _bleIsConnected = isConnected;
        _bleGapConnHandle = connHandle;
    }

    // Callback
    static void registrationCallback(struct ble_gatt_register_ctxt *ctxt, void *arg);

    // Set callback to call when a service is accessed
    static void setServerAccessCB(BLEGattServerAccessCBType callback)
    {
        _accessCallback = callback;
    }

    // Subscription
    static void handleSubscription(struct ble_gap_event * pEvent, uint16_t connHandle);

    // Send to central (using notification)
    static bool sendToCentral(const uint8_t* pBuf, uint32_t bufLen);

    // Check if notification is enabled
    static bool isNotificationEnabled()
    {
        return _responseNotifyState;
    }
    
private:

    // Max size of packet that can be received from NimBLE
    static const int BLE_MAX_RX_PACKET_SIZE = 512;

    // ?????
    static uint8_t gatt_svr_sec_test_static_val;

    // Handles
    static uint16_t _bleGattMessageResponseHandle;
    static bool _bleIsConnected;
    static uint16_t _bleGapConnHandle;

    // State of notify (send from peripheral)
    static bool _responseNotifyState;

    static const struct ble_gatt_svc_def servicesList[];

    // Get data that has been written to characteristic (sent by central)
    static int getDataWrittenToCharacteristic(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
                   void *dst, uint16_t *len);

    // Callback on access to characteristics
    static int commandCharAccess(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);
    static int responseCharAccess(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);

    static BLEGattServerAccessCBType _accessCallback;

    static const uint32_t MIN_TIME_BETWEEN_ERROR_MSGS_MS = 500;
    static uint32_t _lastBLEErrorMsgMs;
    static uint32_t _lastBLEErrorMsgCode;

    // Get HS error message
    static const char* getHSErrorMsg(int errorCode);
};
#endif // CONFIG_BT_ENABLED
