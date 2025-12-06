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
#include <sys/time.h>
#include <time.h>
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
static const ble_uuid16_t CURRENT_TIME_SERVICE_UUID = BLE_UUID16_INIT(0x1805);
static const ble_uuid16_t CURRENT_TIME_CHAR_UUID = BLE_UUID16_INIT(0x2A2B);

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
    else if (serviceConfig.name.equalsIgnoreCase("currentTime"))
    {
        // Set UUID for current time service
        attribDataType = ServiceDataType::CURRENT_TIME;
        pAccessCb = currentTimeAccessCb;
        pServiceUUID = &CURRENT_TIME_SERVICE_UUID.u;
        pCharacteristicUUID = &CURRENT_TIME_CHAR_UUID.u;
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
    // Determine flags based on service config
    uint16_t flags = 0;
    if (serviceConfig.read)
        flags |= BLE_GATT_CHR_F_READ;
    if (serviceConfig.notify)
        flags |= BLE_GATT_CHR_F_NOTIFY;
    if (serviceConfig.indicate)
        flags |= BLE_GATT_CHR_F_INDICATE;
    if (serviceConfig.write)
        flags |= BLE_GATT_CHR_F_WRITE;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    struct ble_gatt_chr_def characteristicList[] = {
        {
            .uuid = pCharacteristicUUID,
            .access_cb = pAccessCb,
            .arg = this,
            .descriptors = nullptr,
            .flags = flags,
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief currentTimeAccessCb - callback for current time characteristic read/write
/// @param conn_handle connection handle
/// @param attr_handle attribute handle
/// @param ctxt context
/// @param arg argument (pointer to BLEStdServices)
/// @return 0 on success, BLE error code on failure
int BLEStdServices::currentTimeAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        // Read current time from system
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        struct tm timeinfo;
        localtime_r(&tv.tv_sec, &timeinfo);

        // Current Time Service format (10 bytes):
        // Year (2 bytes, little endian), Month (1), Day (1), Hours (1), Minutes (1), Seconds (1)
        // Day of week (1), Fractions256 (1), Adjust reason (1)
        uint8_t timeData[10];
        uint16_t year = timeinfo.tm_year + 1900;
        timeData[0] = year & 0xFF;
        timeData[1] = (year >> 8) & 0xFF;
        timeData[2] = timeinfo.tm_mon + 1;  // Month (1-12)
        timeData[3] = timeinfo.tm_mday;     // Day (1-31)
        timeData[4] = timeinfo.tm_hour;     // Hours (0-23)
        timeData[5] = timeinfo.tm_min;      // Minutes (0-59)
        timeData[6] = timeinfo.tm_sec;      // Seconds (0-59)
        timeData[7] = timeinfo.tm_wday + 1; // Day of week (1-7, 1=Monday in BLE spec)
        timeData[8] = (tv.tv_usec * 256) / 1000000;  // Fractions256
        timeData[9] = 0;                    // Adjust reason (0 = unknown)

        os_mbuf_append(ctxt->om, timeData, sizeof(timeData));
        return 0;
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        // Write time to system
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len < 7)  // Minimum: year(2) + month(1) + day(1) + hour(1) + min(1) + sec(1)
        {
            LOG_W(MODULE_PREFIX, "currentTimeAccessCb write data too short: %d bytes", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint8_t timeData[10];
        uint16_t copied = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, timeData, sizeof(timeData), &copied);
        if (rc != 0)
        {
            LOG_W(MODULE_PREFIX, "currentTimeAccessCb failed to copy mbuf data");
            return BLE_ATT_ERR_UNLIKELY;
        }

        // Parse the time data
        struct tm timeinfo = {};
        uint16_t year = timeData[0] | (timeData[1] << 8);
        timeinfo.tm_year = year - 1900;
        timeinfo.tm_mon = timeData[2] - 1;  // Month (0-11 in tm)
        timeinfo.tm_mday = timeData[3];
        timeinfo.tm_hour = timeData[4];
        timeinfo.tm_min = timeData[5];
        timeinfo.tm_sec = timeData[6];
        timeinfo.tm_isdst = -1;  // Let system determine DST

        // Convert to time_t and set system time
        time_t t = mktime(&timeinfo);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        if (settimeofday(&tv, nullptr) == 0)
        {
            LOG_I(MODULE_PREFIX, "currentTimeAccessCb time set to %04d-%02d-%02d %02d:%02d:%02d",
                  year, timeData[2], timeData[3], timeData[4], timeData[5], timeData[6]);
            return 0;
        }
        else
        {
            LOG_W(MODULE_PREFIX, "currentTimeAccessCb settimeofday failed");
            return BLE_ATT_ERR_UNLIKELY;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

#endif // CONFIG_BT_ENABLED
