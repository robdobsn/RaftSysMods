/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Decode Advertisement
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftCore.h"
#include "BLEAdvertDecoder.h"

#define WARN_ON_TOO_MANY_BLE_CLIENTS

// #define DEBUG_BLE_ADVERT_DECODER
// #define DEBUG_BT_HOME_DECODE

#ifdef DEBUG_BLE_ADVERT_DECODER
#define DEBUG_APPEND_LOG(X) logString += (X)
#else
#define DEBUG_APPEND_LOG(X)
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode ad packet
/// @param event BLE discovery event
/// @param fields BLE advertisement fields
/// @param pBusDevicesIF pointer to bus devices interface
/// @return true if the packet was successfully decoded
bool BLEAdvertDecoder::decodeAdEvent(struct ble_gap_event *event, struct ble_hs_adv_fields& fields, RaftBusDevicesIF* pBusDevicesIF)
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
    // if (event->disc.length_data < 3)
    //     return false;

    // // Check for BTHome packet
    // if ((event->disc.data[0] != 0x02) || (event->disc.data[1] != 0x01) || (event->disc.data[2] != 0x06))
    //     return false;
                // Raft::formatMACAddr(event->disc.addr.val, ":", true).c_str(),

    // Iteration of AD structures in the payload
    const uint8_t* pData = event->disc.data;
    int32_t remainingDataLen = ((int)event->disc.length_data);

    // String to accumulate log messages
    String hexStr;
    Raft::getHexStrFromBytes(pData, remainingDataLen, hexStr);
#ifdef DEBUG_BLE_ADVERT_DECODER
    String logString = "decodeAdEvent " + Raft::formatMACAddr(event->disc.addr.val, ":", true) + " " + hexStr + " ";
#endif

    // Iterate through packets
    while (remainingDataLen > 1)
    {
        uint8_t len = pData[0];
        if (len == 0)
            break;
        if (len > remainingDataLen)
            break;
        uint8_t adType = pData[1];

        // Handle the AD types
        switch(adType)
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
                    String name((const char*)(pData+2), len-1);
                    logString += "decodeAdEvent Local name " + name + ", ";
                }
                else
                {
                    logString += "decodeAdEvent Local name TOO SHORT,";
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
                // 16-bit ServiceData UUID
                if (len >= 3)
                {
                    uint16_t uuid = (pData[3] << 8) | pData[2];
                    switch(uuid)
                    {
                        case 0xFEED: DEBUG_APPEND_LOG("decodeAdEvent ServiceData 16-bit UUID Tile Inc len " + String(len-3) + ", "); break;
                        case 0xFCD2: 
                        {
#ifdef DEBUG_BLE_ADVERT_DECODER
                            logString += "decodeAdEvent ServiceData 16-bit UUID BTHome Alterco Robotics len " + String(len-3) + ", ";
                            LOG_I(MODULE_PREFIX, "%s", logString.c_str());
#endif
                            return decodeBtHome(event->disc.addr, pData+4, len-3, pBusDevicesIF);
                        }
                        case 0xFCF1: DEBUG_APPEND_LOG("decodeAdEvent ServiceData 16-bit UUID Google len " + String(len-3) + ", "); break;
                        default: DEBUG_APPEND_LOG("decodeAdEvent ServiceData 16-bit UUID " + String(uuid, 16) + " len " + String(len-3) + ", "); break;
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
        remainingDataLen -= (len + 1);
    }

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
#ifdef DEBUG_BT_HOME_DECODE
    String logStr;
    Raft::getHexStrFromBytes(pBtHomeData, btHomeDataLen, logStr);
#endif

    // Check for minimum length
    if (btHomeDataLen < 1)
        return false;
    
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
    int16_t temperatureX100 = 0;
    uint8_t batteryPC = 0;
    uint32_t illumuninanceX100 = 0;

    // Decode the fields
    while (varDataLen > 0)
    {
        switch(pVarData[0])
        {
            case 0x00: // Packet ID
            {
                if (varDataLen >= 2)
                {
                    packetID = pVarData[1];
#ifdef DEBUG_BT_HOME_DECODE
                    logStr += " PacketID " + String(packetID);
#endif
                }
                varDataLen -= 2;
                pVarData += 2;
                break;
            }
            case 0x01: // Battery
            {
                if (varDataLen >= 2)
                {
                    batteryPC = pVarData[1];
#ifdef DEBUG_BT_HOME_DECODE
                    logStr += " Battery " + String(batteryPC);
#endif
                }
                varDataLen -= 2;
                pVarData += 2;
                break;
            }
            case 0x02: // Temperature
            {
                if (varDataLen >= 3)
                {
                    temperatureX100 = (pVarData[2] << 8) | pVarData[1];
#ifdef DEBUG_BT_HOME_DECODE
                    logStr += " Temp " + String(temperatureX100/100.0, 2);
#endif
                }
                varDataLen -= 3;
                pVarData += 3;
                break;
            }
            case 0x05: // Illuminance
            {
                if (varDataLen >= 4)
                {
                    illumuninanceX100 = (pVarData[2] << 16) | (pVarData[1] << 8) | pVarData[0];
#ifdef DEBUG_BT_HOME_DECODE
                    logStr += " Illum " + String(illumuninanceX100/100.0, 2);
#endif
                }
                varDataLen -= 4;
                pVarData += 4;
                break;
            }
            case 0x21: // Motion
            {
                if (varDataLen >= 2)
                {
                    motion = pVarData[1];
                    dataOfInterest = true;
#ifdef DEBUG_BT_HOME_DECODE
                    logStr += " Motion " + String(motion == 0 ? "NO" : "YES");
#endif
                }
                varDataLen -= 2;
                pVarData += 2;
                break;
            }
        }
    }

    // Check if data is of interest
    if (!dataOfInterest)
        return false;

    // Fill in the decoded data
    std::vector<uint8_t> decodedData;
    // Add the packet ID
    decodedData.push_back(packetID);
    // Add the BLE address
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
    decodedData.push_back(0);
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
    String outStr;
    Raft::getHexStrFromBytes(decodedData.data(), decodedData.size(), outStr);
    LOG_I(MODULE_PREFIX, "decodeBtHome %s => %s", logStr.c_str(), outStr.c_str());
#endif

    return true;
}
