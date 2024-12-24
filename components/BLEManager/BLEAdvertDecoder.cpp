/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Decode Advertisement
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftCore.h"
#include "BLEAdvertDecoder.h"
#include "BTHomeConsts.h"
#include "RaftBusDevicesIF.h"

#ifdef CONFIG_BT_ENABLED

#define WARN_ON_TOO_MANY_BLE_CLIENTS

// #define DEBUG_BLE_ADVERT_DECODER
// #define DEBUG_BT_HOME_DECODE
// #define DEBUG_BLE_ADVERT_DECODER_DETAILS

#ifdef DEBUG_BLE_ADVERT_DECODER
#define DEBUG_APPEND_LOG(X) logString += (X)
#else
#define DEBUG_APPEND_LOG(X)
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode ad packet
/// @param pEvent BLE discovery pEvent
/// @param fields BLE advertisement fields
/// @param pBusDevicesIF pointer to bus devices interface
/// @return true if the packet was successfully decoded
bool BLEAdvertDecoder::decodeAdEvent(struct ble_gap_event *pEvent, struct ble_hs_adv_fields& fields, RaftBusDevicesIF* pBusDevicesIF)
{
    // Debug
    // LOG_I(MODULE_PREFIX, "----- flags %02x num_uuids16 %d num_uuids32 %d num_uuids128 %d tx_pwr_lvl %d adv_itvl %d le_role %d mfg_data_len %d name_len %d", 
    //         fields.flags,
    //         fields.num_uuids16,
    //         fields.num_uuids32,
    //         fields.num_uuids128,
    //         fields.tx_pwr_lvl,
    //         fields.adv_itvl,
    //         fields.le_role,
    //         fields.mfg_data_len,
    //         fields.name_len);


    // // Decode BTHome packet - it must start with 0x020106
    // if (pEvent->disc.length_data < 3)
    //     return false;

    // // Check for BTHome packet
    // if ((pEvent->disc.data[0] != 0x02) || (pEvent->disc.data[1] != 0x01) || (pEvent->disc.data[2] != 0x06))
    //     return false;
                // Raft::formatMACAddr(pEvent->disc.addr.val, ":", true).c_str(),

    // Check there is an interface to send data to
    if (!pBusDevicesIF)
    {
        return false;
    }

    // Check parameters
    if (!pEvent) 
    {
        LOG_W(MODULE_PREFIX, "decodeAdEvent Invalid parameters %p %p", pEvent, pBusDevicesIF);
        return false;
    }

    // Data to decode
    const uint8_t* pData = pEvent->disc.data;
    int32_t remainingDataLen = static_cast<int32_t>(pEvent->disc.length_data);

    // String to accumulate log messages
#ifdef DEBUG_BLE_ADVERT_DECODER
    String hexStr = Raft::getHexStr(pData, remainingDataLen);
    String logString = "decodeAdEvent " + Raft::formatMACAddr(pEvent->disc.addr.val, ":", true) + " " + hexStr + " ";
#endif

#ifdef DEBUG_BLE_ADVERT_DECODER_DETAILS
    LOG_I(MODULE_PREFIX, "%s", logString.c_str());
    delay(5);
#endif

    const uint32_t MAX_BLE_DECODE_LOOPS = 20;

    // Iterate through packets
    uint32_t loopCnt = 0;
    while (remainingDataLen > 2 && loopCnt++ < MAX_BLE_DECODE_LOOPS)
    {
        uint8_t len = pData[0];
        if (len == 0 || len > remainingDataLen)
            break;
        uint8_t adType = pData[1];

        // Handle the AD types
        switch (adType)
        {
            case 0x01:
            {
                // Flags
                if (len >= 2)
                {
                    uint8_t flags = pData[2];
                    String flagString;
                    if (flags & 0x01)
                        flagString += "LE_LIM_DISC ";
                    if (flags & 0x02)
                        flagString += "LE_GEN_DISC ";
                    if (flags & 0x04)
                        flagString += "BR_EDR_NON_SUP ";
                    if (flags & 0x08)
                        flagString += "SIMUL_LE_BREDR_CTRL ";
                    if (flags & 0x10)
                        flagString += "SIMUL_LE_BREDR_HOST ";
                    DEBUG_APPEND_LOG("decodeAdEvent Flags " + String(flags, 16) + " " + flagString + ", ");
                }
                break;
            }
            case 0x02: 
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07: 
            {
                DEBUG_APPEND_LOG("decodeAdEvent Incomplete list " + String(adType, 16) + ", ");
                break;
            }
            case 0x08:
            case 0x09:
            {
                // Local name
#ifdef DEBUG_BLE_ADVERT_DECODER
                if (len > 1)
                {
                    String name(reinterpret_cast<const char*>(pData+2), len-1);
                    logString += "local name " + name + ", ";
                }
                else
                {
                    logString += "local name TOO SHORT,";
                }
#endif
                break;
            }
            case 0x0a: DEBUG_APPEND_LOG("decodeAdEvent TxPowerLevel " + String(pData[2], 16) + ", "); break;
            case 0x0d: DEBUG_APPEND_LOG("decodeAdEvent ClassOfDevice, "); break;
            case 0x0e: DEBUG_APPEND_LOG("decodeAdEvent SimplePairingHashC, "); break;
            case 0x0f: DEBUG_APPEND_LOG("decodeAdEvent SimplePairingRandomizerR, "); break;
            case 0x10: DEBUG_APPEND_LOG("decodeAdEvent DeviceID, "); break;
            case 0x12: DEBUG_APPEND_LOG("decodeAdEvent SecurityManagerOOBFlags, "); break;
            case 0x13: DEBUG_APPEND_LOG("decodeAdEvent SlaveConnectionIntervalRange, "); break;
            case 0x15: DEBUG_APPEND_LOG("decodeAdEvent ServiceSolicitationUUIDs, "); break;
            case 0x16:
            {
                // 16-bit Service Data UUID
                if (len >= 3)
                {
                    uint16_t uuid = (pData[3] << 8) | pData[2];
                    switch (uuid)
                    {
                    case 0xFEED:
                        DEBUG_APPEND_LOG("decodeAdEvent ServiceData 16-bit UUID Tile Inc len " + String(len - 3) + ", ");
                        break;
                    case 0xFCD2:
                    {
#ifdef DEBUG_BLE_ADVERT_DECODER
                        logString += "decodeAdEvent ServiceData 16-bit UUID BTHome Alterco Robotics len " + String(len - 3) + ", ";
                        LOG_I(MODULE_PREFIX, "%s", logString.c_str());
#endif
                        return decodeBtHome(pEvent->disc.addr, pData + 4, len - 3, pBusDevicesIF);
                    }
                    case 0xFCF1:
                        DEBUG_APPEND_LOG("decodeAdEvent ServiceData 16-bit UUID Google len " + String(len - 3) + ", ");
                        break;
                    default:
                        DEBUG_APPEND_LOG("decodeAdEvent ServiceData 16-bit UUID " + String(uuid, 16) + " len " + String(len - 3) + ", ");
                        break;
                    }
                }
                break;
            }
            case 0x17: DEBUG_APPEND_LOG("decodeAdEvent PublicTargetAddress, "); break;
            case 0x18: DEBUG_APPEND_LOG("decodeAdEvent RandomTargetAddress, "); break;
            case 0x19: DEBUG_APPEND_LOG("decodeAdEvent Appearance len " + String(len) + ", "); break;
            case 0x1a: DEBUG_APPEND_LOG("decodeAdEvent AdvertisingInterval, "); break;
            case 0x1b: DEBUG_APPEND_LOG("decodeAdEvent LEBluetoothDeviceAddress, "); break;
            case 0x1c: DEBUG_APPEND_LOG("decodeAdEvent LERole, "); break;
            case 0x20: DEBUG_APPEND_LOG("decodeAdEvent ServiceData 32bit UUID, "); break;
            case 0x21: DEBUG_APPEND_LOG("decodeAdEvent ServiceData 128bit UUID, "); break;
            case 0x22: DEBUG_APPEND_LOG("decodeAdEvent LE Secure Connections Confirmation Value, "); break;
            case 0x23: DEBUG_APPEND_LOG("decodeAdEvent LE Secure Connections Random Value, "); break;
            case 0x24: DEBUG_APPEND_LOG("decodeAdEvent URI, "); break;
            case 0x25: DEBUG_APPEND_LOG("decodeAdEvent IndoorPositioning, "); break;
            case 0x26: DEBUG_APPEND_LOG("decodeAdEvent TransportDiscoveryData, "); break;
            case 0x27: DEBUG_APPEND_LOG("decodeAdEvent LE Supported Features, "); break;
            case 0x28: DEBUG_APPEND_LOG("decodeAdEvent ChannelMapUpdateIndication, "); break;
            case 0x29: DEBUG_APPEND_LOG("decodeAdEvent PB-ADV, "); break;
            case 0x2a: DEBUG_APPEND_LOG("decodeAdEvent MeshMessage, "); break;
            case 0x2b: DEBUG_APPEND_LOG("decodeAdEvent MeshBeacon, "); break;
            case 0x3d: DEBUG_APPEND_LOG("decodeAdEvent 3DInformationData, "); break;
            case 0xff: DEBUG_APPEND_LOG("decodeAdEvent ManufacturerSpecificData len " + String(len) + ", "); break;
            default:
            {
                DEBUG_APPEND_LOG("decodeAdEvent adType " + String(adType, 16) + " len " + String(len) + ", ");
                break;
            }
        }

        // Move to next packet
        pData += len + 1;
        remainingDataLen -= len + 1;
    }

#ifdef DEBUG_BLE_ADVERT_DECODER_DETAILS
    LOG_I(MODULE_PREFIX, "decodeAdEvent exiting");
    delay(5);
#endif

    // Log the accumulated messages
#ifdef DEBUG_BLE_ADVERT_DECODER
    LOG_I(MODULE_PREFIX, "%s", logString.c_str());
#endif
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode BTHome 
/// @param bleAddr BLE address
/// @param pBtHomeData BTHome data
/// @param btHomeDataLen BTHome data length
/// @param pBusDevicesIF pointer to bus devices interface
/// @return true if the packet was successfully decoded
bool BLEAdvertDecoder::decodeBtHome(ble_addr_t bleAddr, const uint8_t* pBtHomeData, int btHomeDataLen, RaftBusDevicesIF* pBusDevicesIF)
{
    if (!pBtHomeData || btHomeDataLen < 1 || !pBusDevicesIF) 
    {
        LOG_W(MODULE_PREFIX, "decodeBtHome Invalid parameters");
        return false;
    }
#ifdef DEBUG_BT_HOME_DECODE
    String logStr;
    Raft::getHexStrFromBytes(pBtHomeData, btHomeDataLen, logStr);
    logStr = Raft::formatMACAddr(bleAddr.val, ":", true) + " " + logStr + " ";
#endif

#ifdef DEBUG_BT_HOME_DECODE
    // Get the BTHome Device Information
    uint8_t btHomeDeviceInfo = pBtHomeData[0];
    bool isEncrypted = (btHomeDeviceInfo & 0x01) != 0;
    bool isTriggerBased = (btHomeDeviceInfo & 0x04) != 0;
    uint8_t btHomeVersion = (btHomeDeviceInfo >> 5) & 0x07;
    logStr += " DevInfo " + String(isEncrypted ? "ENC " : "NOENC ") + String(isTriggerBased ? "TRIG " : "NO_TRIG ") + "Ver " + String(btHomeVersion);
#endif

    // Start of variable data
    const uint8_t* pVarData = pBtHomeData + 1;
    int varDataLen = btHomeDataLen - 1;

    // Fields of interest
    uint8_t packetID = 0;
    bool motion = false;
    bool dataOfInterest = false;
    int16_t temperatureX100 = INT16_MAX;
    uint8_t batteryPC = UINT8_MAX;
    uint32_t illumuninanceX100 = UINT32_MAX;

    // Decode the fields
    static const uint32_t MAX_BTHOME_FIELDS = 20;
    uint32_t loopCnt = 0;
    while (varDataLen >= 2 && loopCnt++ < MAX_BTHOME_FIELDS) 
    {
        // Sensor type
        uint8_t sensorType = pVarData[0];

        // Get length from table
        int8_t fieldLen = -1;
        if (sensorType == 0x53) // Text
            fieldLen = pVarData[1];
        else if (sensorType == 0x54) // Binary
            fieldLen = pVarData[1];
        else if (sensorType < BTHOME_SENSOR_TYPE_COUNT) // Use LUT
            fieldLen = BTHOME_SENSOR_TYPES[sensorType].len;
        else if (sensorType == 0xf0) // device type ID
            fieldLen = 2;
        else if (sensorType == 0xf1) // firmware version
            fieldLen = 4;
        else if (sensorType == 0xf2) // firmware version
            fieldLen = 3;

        // Check for valid length
        if (fieldLen < 0 || varDataLen < fieldLen + 1)
        {
#ifdef DEBUG_BT_HOME_DECODE
            logStr += " INVALID FIELD " + String(sensorType, 16) + " " + String(fieldLen) + " varDataLen " + String(varDataLen);
#endif
            break;
        }

        // Decode the field
        switch(sensorType)
        {
            case 0x00: // Packet ID
            {
                packetID = pVarData[1];
#ifdef DEBUG_BT_HOME_DECODE
                logStr += " PacketID " + String(packetID);
#endif
                break;
            }
            case 0x01: // Battery
            {
                batteryPC = pVarData[1];
#ifdef DEBUG_BT_HOME_DECODE
                logStr += " Battery " + String(batteryPC);
#endif
                break;
            }
            case 0x02: // Temperature
            {
                temperatureX100 = (pVarData[2] << 8) | pVarData[1];
#ifdef DEBUG_BT_HOME_DECODE
                logStr += " Temp " + String(temperatureX100/100.0, 2);
#endif
                break;
            }
            case 0x05: // Illuminance
            {
                illumuninanceX100 = (pVarData[2] << 16) | (pVarData[1] << 8) | pVarData[0];
#ifdef DEBUG_BT_HOME_DECODE
                logStr += " Illum " + String(illumuninanceX100/100.0, 2);
#endif
                break;
            }
            case 0x21: // Motion
            {
                motion = pVarData[1];
                dataOfInterest = true;
#ifdef DEBUG_BT_HOME_DECODE
                logStr += " Motion " + String(motion == 0 ? "NO" : "YES");
#endif
                break;
            }
        }

        // Move to next field
        varDataLen -= fieldLen + 1;
        pVarData += fieldLen + 1;
    }

    // Check if data is of interest
    if (!dataOfInterest)
    {
#ifdef DEBUG_BT_HOME_DECODE
        logStr += " NO DATA OF INTEREST";
        LOG_I(MODULE_PREFIX, "decodeBtHome %s", logStr.c_str());
#endif
        return false;
    }

    // Fill in the decoded data
    std::vector<uint8_t> decodedData;
    uint16_t timeVal = (uint16_t)(millis() & 0xFFFF);
    decodedData.push_back((timeVal >> 8) & 0xFF);
    decodedData.push_back(timeVal & 0xFF);    
    // Add the packet ID
    // Note that this MUST be at the position indicated by DUPLICATE_RECORD_DEVICE_ID_POS
    decodedData.push_back(packetID);
    // Add the BLE address (padded to 8 bytes)
    decodedData.push_back(0);
    decodedData.push_back(0);
    for (int i = 0; i < 6; i++)
        decodedData.push_back(bleAddr.val[5-i]);
    // Add the motion
    decodedData.push_back(motion);
    // Add the battery
    decodedData.push_back(batteryPC);
    // Add the temperature
    decodedData.push_back((temperatureX100 >> 8) & 0xff);
    decodedData.push_back(temperatureX100 & 0xff);
    // Add the illuminance
    decodedData.push_back((illumuninanceX100 >> 24) & 0xff);
    decodedData.push_back((illumuninanceX100 >> 16) & 0xff);
    decodedData.push_back((illumuninanceX100 >> 8) & 0xff);
    decodedData.push_back(illumuninanceX100 & 0xff);

    // We need a 32 bit version of the address - so XOR the 3 manufacturer bytes together in the top byte
    uint32_t bleAddr32 = (bleAddr.val[5] ^ bleAddr.val[4] ^ bleAddr.val[3]) << 24;
    bleAddr32 |= (bleAddr.val[2] << 16) | (bleAddr.val[1] << 8) | bleAddr.val[0];

    // Update the interface
    if (pBusDevicesIF)
        pBusDevicesIF->handlePollResult(micros(), bleAddr32, decodedData, nullptr);

#ifdef DEBUG_BT_HOME_DECODE
    String outStr = Raft::getHexStr(decodedData.data(), decodedData.size());
    LOG_I(MODULE_PREFIX, "decodeBtHome %s => %s", logStr.c_str(), outStr.c_str());
#endif

    return true;
}

#endif
