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
#include <CommsChannelMsg.h>
#include <vector>

#include <functional>
#undef min
#undef max
#include <host/ble_hs.h>
#undef min
#undef max

// Callback types
typedef std::function<void (const char* characteristicName, bool readOp, 
                std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> rxMsg)> BLEGattServerAccessCBType;
typedef int (*BLECharAccessFnType)(uint16_t conn_handle, uint16_t attr_handle,
                                              struct ble_gatt_access_ctxt *ctxt,
                                              void *arg);
// Send result
enum BLEGattServerSendResult
{
    BLEGATT_SERVER_SEND_RESULT_OK,
    BLEGATT_SERVER_SEND_RESULT_FAIL,
    BLEGATT_SERVER_SEND_RESULT_TRY_AGAIN
};

class BLEGattServer
{
public:
    // UUIDs
    static ble_uuid128_t DEFAULT_MAIN_SERVICE_UUID;
    static ble_uuid128_t DEFAULT_MESSAGE_COMMAND_UUID;
    static ble_uuid128_t DEFAULT_MESSAGE_RESPONSE_UUID;

    // Constructor
    BLEGattServer(BLEGattServerAccessCBType callback, BLEManStats& bleStats);
    virtual ~BLEGattServer();

    // Setup
    bool setup(uint32_t maxPacketLen, uint32_t outboundQueueSize, bool useTaskForSending,
                UBaseType_t taskCore, BaseType_t taskPriority, int taskStackSize,
                bool sendUsingIndication,
                const String& uuidCmdRespService,
                const String& uuidCmdRespCommand,
                const String& uuidCmdRespResponse,
                bool batteryService,
                bool deviceInfoService,
                bool heartRate);

    // Service
    void service();

    // Sending
    bool isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
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
    BLEGattServerSendResult sendToCentral(const uint8_t* pBuf, uint32_t bufLen);

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

    // Get UUID for main service
    ble_uuid128_t& getMainServiceUUID128()
    {
        return _mainServiceUUID128;
    }

private:

    // At registration time this is filled with the charactteristic's value attribute handle
    uint16_t _characteristicValueAttribHandle = 0;

    // Send using indication
    bool _sendUsingIndication = false;

    // Access callback
    BLEGattServerAccessCBType _accessCallback = nullptr;

    // Connection info
    bool _bleIsConnected = false;
    uint16_t _bleGapConnHandle = 0;

    // State of notify (send from peripheral)
    bool _responseNotifyState = false;

    // Services and characteristics lists
    std::vector<struct ble_gatt_svc_def> servicesList;
    std::vector<struct ble_gatt_chr_def> mainServiceCharList;
    std::vector<struct ble_gatt_chr_def> batteryServiceCharList;

    // Get data that has been written to characteristic (sent by central/client)
    int getDataWrittenToCharacteristic(struct os_mbuf *om, std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& rxMsg);

    // Callback on access to characteristics
    int commandCharAccess(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);
    int responseCharAccess(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);

    // Error message handling
    static const uint32_t MIN_TIME_BETWEEN_ERROR_MSGS_MS = 500;
    uint32_t _lastBLEErrorMsgMs = 0;
    uint32_t _lastBLEErrorMsgCode = 0;

    // Outbound handler
    BLEGattOutbound _bleOutbound;

    // UUIDs
    ble_uuid128_t _mainServiceUUID128 = DEFAULT_MAIN_SERVICE_UUID;
    ble_uuid128_t _commandUUID128 = DEFAULT_MESSAGE_COMMAND_UUID;
    ble_uuid128_t _responseUUID128 = DEFAULT_MESSAGE_RESPONSE_UUID;

    // Standard services
    bool _batteryService = false;
    bool _deviceInfoService = false;
    bool _heartRate = false;
};
#endif // CONFIG_BT_ENABLED
