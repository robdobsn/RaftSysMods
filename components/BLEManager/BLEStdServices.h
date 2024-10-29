/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEStdServices.h
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "sdkconfig.h"
#include "BLEConsts.h"
#include "BLEConfig.h"
#include "NamedValueProvider.h"
#include <list>

#ifdef CONFIG_BT_ENABLED

class BLEStdServices
{
public:
    /// @brief setup
    /// @param serviceConfigs Configuration for standard services 
    /// @param servicesList List of services to add to
    void setup(std::vector<BLEStandardServiceConfig>& serviceConfigs, std::vector<struct ble_gatt_svc_def>& servicesList);

    /// @brief updateStdServices
    /// @param conn_handle GAP connection handle
    void updateStdServices(uint16_t conn_handle, NamedValueProvider* pNamedValueProvider);

    // Fixed values for system information - must not be changed after setup
    static String systemManufacturer;
    static String systemModel;
    static String systemSerialNumber;
    static String firmwareVersionNumber;
    static String hardwareRevisionNumber;

private:
    
    // Enum for data types
    enum class ServiceDataType
    {
        BYTE,
        FLAG0_AND_BYTE
    };

    // Standard services
    struct StandardService
    {
        String serviceName;
        bool enable:1 = false;
        bool notify:1 = false;
        bool indicate:1 = false;
        bool read:1 = false;
        String serviceSettings;
        uint16_t attribHandle = 0;
        ServiceDataType attribType = ServiceDataType::BYTE;
        double attribValue;
        uint32_t updateIntervalMs = 0;
        uint32_t lastUpdateTimeMs = 0;
        std::vector<struct ble_gatt_chr_def> characteristicList;
    };
    std::list<StandardService> _standardServices;

    // Format attribute data for BLE
    static void formatAttributeData(StandardService& service, std::vector<uint8_t>& data)
    {
        // Check data type
        if (service.attribType == ServiceDataType::BYTE)
        {
            uint8_t byteVal = (uint8_t)service.attribValue;
            data.push_back(byteVal);
        }
        else if (service.attribType == ServiceDataType::FLAG0_AND_BYTE)
        {
            uint8_t byteVal = (uint8_t)service.attribValue;
            data.push_back(0);
            data.push_back(byteVal);
        }
    }

    // Static callback function to single byte values
    static int attribValueAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            std::vector<uint8_t> data;
            formatAttributeData(static_cast<BLEStdServices*>(arg)->_standardServices.back(), data);
            os_mbuf_append(ctxt->om, data.data(), data.size());
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    // Static callback function to handle device info access
    static int deviceInfoAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg) {
        const char* info = static_cast<const char*>(arg);
        os_mbuf_append(ctxt->om, info, strlen(info));
        return 0;
    }

    // Overall service update time
    static const uint32_t OVERALL_SERVICE_UPDATE_INTERVAL_MS = 500;
    uint32_t _lastServiceUpdateTimeMs = 0;

    // Setup service helpers
    void setupService(const BLEStandardServiceConfig& serviceConfig, std::vector<struct ble_gatt_svc_def>& servicesList);
    void setupDeviceInfoService(const BLEStandardServiceConfig& serviceConfig, std::vector<struct ble_gatt_svc_def>& servicesList);

    // Debug
    static constexpr const char *MODULE_PREFIX = "BLEStdServices";
};

#endif // CONFIG_BT_ENABLED
