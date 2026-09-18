/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEGattServer
// Handles BLE GATT
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "sdkconfig.h"
#include "BLEConsts.h"
#include "BLEConfig.h"
#include "BLEStdServices.h"

// Only compile in this code if BLE is enabled to reduce code size
#ifdef CONFIG_BT_ENABLED

#include "BLEStdServices.h"
#include "BLEGattOutbound.h"
#include "CommsChannelMsg.h"
#include <vector>
#include <atomic>

#include <functional>
#undef min
#undef max
#include "host/ble_hs.h"
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
    bool setup(const BLEConfig& bleConfig);

    // Service
    void loop(NamedValueProvider* pNamedValueProvider);

    // Sending
    bool isReadyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
    bool sendMsg(CommsChannelMsg& msg);

    // Set connection handle (may be called on the NimBLE host task)
    void setConnState(bool isConnected, uint16_t connHandle)
    {
        _bleGapConnHandle = isConnected ? connHandle : BLE_HS_CONN_HANDLE_NONE;
    }

    // Callback
    static void registrationCallbackStatic(struct ble_gatt_register_ctxt *ctxt, void *arg);

    // Subscription
    void handleSubscription(struct ble_gap_event * pEvent, String& statusStr);

    // Send to central (using notification or indication)
    BLEGattServerSendResult sendToCentral(const uint8_t* pBuf, uint32_t bufLen, bool useIndication);

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

    // Get UUID for main loop
    ble_uuid128_t& getMainServiceUUID128()
    {
        return _mainServiceUUID128;
    }

    // Get max packet length
    uint32_t getMaxPacketLen()
    {
        return _maxPacketLen;
    }

    // Get preferred MTU size
    uint32_t getPreferredMTUSize()
    {
        return _bleOutbound.getPreferredMTUSize();
    }

private:

    // Enabled flag
    bool _isEnabled = false;

    // At registration time this is filled with the charactteristic's value attribute handle
    uint16_t _characteristicValueAttribHandle = 0;

    // Send using indication
    bool _sendUsingIndication = false;

    // Max packet length
    uint32_t _maxPacketLen = 0;

    // Access callback
    BLEGattServerAccessCBType _accessCallback = nullptr;

    // Connection handle - BLE_HS_CONN_HANDLE_NONE when not connected
    // This is written on the NimBLE host task and should be snapshotted once in any function that uses it
    std::atomic<uint16_t> _bleGapConnHandle{BLE_HS_CONN_HANDLE_NONE};

    // State of notify (send from peripheral) - written on the NimBLE host task
    std::atomic<bool> _responseNotifyState{false};

    // Build service tables (called once only from start())
    void buildServiceTables();

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

    // All services
    std::vector<struct ble_gatt_svc_def> _servicesList;

    // Custom service and characteristics
    std::vector<struct ble_gatt_chr_def> _mainServiceCharList;

    // Flag indicating the service tables (above and in standard services) have been built
    // The tables are built once only as they contain pointers into each other and the BLE stack
    // is given pointers to them (so they must not be reallocated if the BLE stack is restarted)
    bool _serviceTablesBuilt = false;

    // Standard services config
    std::vector<BLEStandardServiceConfig> _stdServicesConfig;

    // Standard services
    BLEStdServices _stdServices;

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "BLEGattSrv";
};
#endif // CONFIG_BT_ENABLED
