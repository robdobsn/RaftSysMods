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

#include "BLEGattOutbound.h"

#include <functional>
#undef min
#undef max
#include <host/ble_hs.h>
#undef min
#undef max

// Callback types
typedef std::function<void (const char* characteristicName, bool readOp, const uint8_t *payloadbuffer, int payloadlength)> BLEGattServerAccessCBType;

class BLEGattServer
{
public:
    // UUIDs
    static ble_uuid128_t GATT_RICV2_MAIN_SERVICE_UUID;
    static ble_uuid128_t GATT_RICV2_MESSAGE_COMMAND_UUID;
    static ble_uuid128_t GATT_RICV2_MESSAGE_RESPONSE_UUID;

    // Constructor
    BLEGattServer(BLEGattServerAccessCBType callback, BLEManStats& bleStats);
    virtual ~BLEGattServer();

    // Setup
    bool setup(uint32_t maxPacketLen, uint32_t outboundQueueSize, bool useTaskForSending,
                UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize);

    // Service
    void service();

    // Sending
    bool isReadyToSend(uint32_t channelID, bool& noConn);
    bool sendMsg(CommsChannelMsg& msg);

    // Set connection handle
    void setConnState(bool isConnected, uint16_t connHandle)
    {
        _bleIsConnected = isConnected;
        _bleGapConnHandle = connHandle;
    }

    // Callback
    static void registrationCallbackStatic(struct ble_gatt_register_ctxt *ctxt, void *arg);

    // Subscription
    void handleSubscription(struct ble_gap_event * pEvent, String& statusStr);

    // Send to central (using notification)
    bool sendToCentral(const uint8_t* pBuf, uint32_t bufLen);

    // Check if notification is enabled
    bool isNotificationEnabled()
    {
        return _responseNotifyState;
    }
    
    // Start and stop
    int start();
    void stop();

    // Get HS error message
    static String getHSErrorMsg(int errorCode);

    // Outbound
    BLEGattOutbound& getOutbound()
    {
        return _bleOutbound;
    }

private:

    // Singleton
    static BLEGattServer* _pThis;

    // At registration time this is filled with the charactteristic's value attribute handle
    static uint16_t _characteristicValueAttribHandle;

    // Max size of packet that can be received from NimBLE
    static const int BLE_MAX_RX_PACKET_SIZE = 512;

    // ?????
    uint8_t gatt_svr_sec_test_static_val;

    // Access callback
    BLEGattServerAccessCBType _accessCallback = nullptr;

    // Connection info
    bool _bleIsConnected;
    uint16_t _bleGapConnHandle;

    // State of notify (send from peripheral)
    bool _responseNotifyState;

    // Services list
    static const struct ble_gatt_svc_def servicesList[];

    // Get data that has been written to characteristic (sent by central/client)
    int getDataWrittenToCharacteristic(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
                   void *dst, uint16_t *len);

    // Callback on access to characteristics
    int commandCharAccess(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);
    int responseCharAccess(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);

    const uint32_t MIN_TIME_BETWEEN_ERROR_MSGS_MS = 500;
    uint32_t _lastBLEErrorMsgMs;
    uint32_t _lastBLEErrorMsgCode;

    // Outbound handler
    BLEGattOutbound _bleOutbound;

};
#endif // CONFIG_BT_ENABLED
