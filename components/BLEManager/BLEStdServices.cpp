/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEStdServices
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "sdkconfig.h"

#ifdef CONFIG_BT_ENABLED

// #define DEBUG_BLE_STD_SERVICES

#include <string.h>
#include "Logger.h"
#include "RaftUtils.h"
#include "RaftArduino.h"
#include "BLEStdServices.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// BLE UUIDs
static const ble_uuid16_t BATTERY_SERVICE_UUID = BLE_UUID16_INIT(0x180F);
static const ble_uuid16_t GATT_CHR_UUID_BATTERY_LEVEL = BLE_UUID16_INIT(0x2A19);
static const ble_uuid16_t DEVICE_INFO_SERVICE_UUID = BLE_UUID16_INIT(0x180A);
static const ble_uuid16_t MANUFACTURER_NAME_UUID = BLE_UUID16_INIT(0x2A29);
static const ble_uuid16_t MODEL_NUMBER_UUID = BLE_UUID16_INIT(0x2A24);
static const ble_uuid16_t SERIAL_NUMBER_UUID = BLE_UUID16_INIT(0x2A25);
static const ble_uuid16_t FIRMWARE_REVISION_UUID = BLE_UUID16_INIT(0x2A26);
static const ble_uuid16_t HARDWARE_REVISION_UUID = BLE_UUID16_INIT(0x2A27);
static const ble_uuid16_t HEART_RATE_SERVICE_UUID = BLE_UUID16_INIT(0x180D);
static const ble_uuid16_t HEART_RATE_MEASUREMENT_UUID = BLE_UUID16_INIT(0x2A37);

// Statics
String BLEStdServices::systemManufacturer;
String BLEStdServices::systemModel;
String BLEStdServices::systemSerialNumber;
String BLEStdServices::firmwareVersionNumber;
String BLEStdServices::hardwareRevisionNumber;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief setup
/// @param serviceConfigs configuration for standard services
/// @param servicesList list of services to add to
void BLEStdServices::setup(std::vector<BLEStandardServiceConfig>& serviceConfigs, std::vector<struct ble_gatt_svc_def>& servicesList)
{    
    // Iterate through standard services config
    for (const auto& stdServiceCfg : serviceConfigs)
    {
        // Setup the service
        setupService(stdServiceCfg, servicesList);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief updateStdServices
/// @param gapConnHandle GAP connection handle
/// @param pNamedValueProvider named value provider
void BLEStdServices::updateStdServices(uint16_t gapConnHandle, NamedValueProvider* pNamedValueProvider)
{
    // Check if time to for services update
    if (Raft::isTimeout(millis(), _lastServiceUpdateTimeMs, OVERALL_SERVICE_UPDATE_INTERVAL_MS))
    {
        // Update timeout
        _lastServiceUpdateTimeMs = millis();

        // Iterate through configured services
        for (auto& stdService : _standardServices)
        {
            // Check if service is enabled and namedvalue provider is valid
            if (!stdService.enable || !pNamedValueProvider)
                continue;

            // Check if time to for update
            if ((stdService.updateIntervalMs != 0) && Raft::isTimeout(millis(), stdService.lastUpdateTimeMs, stdService.updateIntervalMs))
            {
                // Update last notify time
                stdService.lastUpdateTimeMs = millis();

                // Get SysMod and namedValue used to access the attribute
                RaftJson settings(stdService.serviceSettings);
                String sysModName = settings.getString("sysMod", "");
                String namedValueName = settings.getString("namedValue", "");

                // Get attribute value
                bool isValid = false;
                stdService.attribValue = pNamedValueProvider->getNamedValue(sysModName.c_str(), namedValueName.c_str(), isValid);
                if (!isValid)
                    continue;

                // Check if service is notify or indicate
                if (stdService.notify || stdService.indicate)
                {

                    // Get mbuf based on service type
                    std::vector<uint8_t> data;
                    formatAttributeData(stdService, data);
                    struct os_mbuf* pMbuf = ble_hs_mbuf_from_flat(data.data(), data.size());
                    if (!pMbuf)
                        continue;

                    // Send update
                    if (stdService.notify)
                    {
                        // Notify
                        ble_gatts_notify_custom(gapConnHandle, stdService.attribHandle, pMbuf);
                    }
                    else if (stdService.indicate)
                    {
                        // Indicate
                        ble_gatts_indicate_custom(gapConnHandle, stdService.attribHandle, pMbuf);
                    }

                    // Debug
#ifdef DEBUG_BLE_STD_SERVICES
                    LOG_I(MODULE_PREFIX, "updateStdServices service %s notify %d indicate %d read %d value %.2f",
                        stdService.serviceName.c_str(), stdService.notify, stdService.indicate, stdService.read, stdService.attribValue);
#endif
                }
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief setupService 
/// @param serviceConfig configuration for standard service
/// @param servicesList list of services to add to
void BLEStdServices::setupService(const BLEStandardServiceConfig& serviceConfig, std::vector<struct ble_gatt_svc_def>& servicesList)
{
    // Handle different services
    ServiceDataType attribDataType = ServiceDataType::BYTE;
    ble_gatt_access_fn* pAccessCb = attribValueAccessCb;
    const ble_uuid_t* pServiceUUID = nullptr;
    const ble_uuid_t* pCharacteristicUUID = nullptr;
    if (serviceConfig.name.equalsIgnoreCase("deviceInfo"))
    {
        // Device info has its own special setup
        setupDeviceInfoService(serviceConfig, servicesList);
        return;
    }
    else if (serviceConfig.name.equalsIgnoreCase("battery"))
    {
        // Set UUID for battery service
        pServiceUUID = &BATTERY_SERVICE_UUID.u;
        pCharacteristicUUID = &GATT_CHR_UUID_BATTERY_LEVEL.u;
    }
    else if (serviceConfig.name.equalsIgnoreCase("heartRate"))
    {
        // Set UUID for heart rate service which uses a flag and byte format
        attribDataType = ServiceDataType::FLAG0_AND_BYTE;
        pServiceUUID = &HEART_RATE_SERVICE_UUID.u;
        pCharacteristicUUID = &HEART_RATE_MEASUREMENT_UUID.u;
    }
    else
    {
        // Unknown service
        LOG_W(MODULE_PREFIX, "setupAttributeValueService unknown service %s", serviceConfig.name.c_str());
        return;
    }

    // Service information
    StandardService service;
    service.serviceName = serviceConfig.name;
    service.enable = serviceConfig.enable;
    service.notify = serviceConfig.notify;
    service.indicate = serviceConfig.indicate;
    service.read = serviceConfig.read;
    service.attribType = attribDataType;
    service.serviceSettings = serviceConfig.serviceSettings;
    service.updateIntervalMs = serviceConfig.updateIntervalMs;
    _standardServices.push_back(service);

    // Service characteristics
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    struct ble_gatt_chr_def characteristicList[] = {
        {
            .uuid = pCharacteristicUUID,
            .access_cb = pAccessCb,
            .arg = this,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = 0,
            .val_handle = &_standardServices.back().attribHandle,
            .cpfd = nullptr
        },
        {0}
    };
#pragma GCC diagnostic pop
    for (int i = 0; i < sizeof(characteristicList) / sizeof(characteristicList[0]); i++)
    {
        _standardServices.back().characteristicList.push_back(characteristicList[i]);
    }

    // Service definition
    struct ble_gatt_svc_def serviceDefn = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = pServiceUUID,
        .includes = nullptr,
        .characteristics = _standardServices.back().characteristicList.data(),
    };

    servicesList.push_back(serviceDefn);

    // Debug
#ifdef DEBUG_BLE_STD_SERVICES
    LOG_I(MODULE_PREFIX, "setupAttributeValueService %s settings %s enable %d notify %d indicate %d read %d",
        service.serviceName.c_str(), service.serviceSettings.c_str(), 
        service.enable, service.notify, service.indicate, service.read);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief setupDeviceInfoService
/// @param serviceConfig configuration for standard service
/// @param servicesList list of services to add to
void BLEStdServices::setupDeviceInfoService(const BLEStandardServiceConfig& serviceConfig, std::vector<struct ble_gatt_svc_def>& servicesList) 
{
    StandardService service;
    service.serviceName = serviceConfig.name;
    service.enable = serviceConfig.enable;
    service.read = true;
    _standardServices.push_back(service);

    // Device Info characteristics
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    struct ble_gatt_chr_def characteristicList[] = {
        {
            .uuid = &MANUFACTURER_NAME_UUID.u,
            .access_cb = deviceInfoAccessCb,
            .arg = (void*)systemManufacturer.c_str(),
            .flags = BLE_GATT_CHR_F_READ
        },
        {
            .uuid = &MODEL_NUMBER_UUID.u,
            .access_cb = deviceInfoAccessCb,
            .arg = (void*)systemModel.c_str(),
            .flags = BLE_GATT_CHR_F_READ
        },
        {
            .uuid = &SERIAL_NUMBER_UUID.u,
            .access_cb = deviceInfoAccessCb,
            .arg = (void*)systemSerialNumber.c_str(),
            .flags = BLE_GATT_CHR_F_READ
        },
        {
            .uuid = &FIRMWARE_REVISION_UUID.u,
            .access_cb = deviceInfoAccessCb,
            .arg = (void*)firmwareVersionNumber.c_str(),
            .flags = BLE_GATT_CHR_F_READ
        },
        {
            .uuid = &HARDWARE_REVISION_UUID.u,
            .access_cb = deviceInfoAccessCb,
            .arg = (void*)hardwareRevisionNumber.c_str(),
            .flags = BLE_GATT_CHR_F_READ
        },        
        {0}  // Null terminator
    };
#pragma GCC diagnostic pop
    for (int i = 0; i < sizeof(characteristicList) / sizeof(characteristicList[0]); i++)
    {
        _standardServices.back().characteristicList.push_back(characteristicList[i]);
    }

    // Define the Device Information Service
    struct ble_gatt_svc_def deviceInfoService = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &DEVICE_INFO_SERVICE_UUID.u,
        .includes = nullptr,
        .characteristics = _standardServices.back().characteristicList.data(),
    };

    servicesList.push_back(deviceInfoService);
}

#endif // CONFIG_BT_ENABLED
