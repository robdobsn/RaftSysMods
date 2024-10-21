/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Decode Advertisement
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftCore.h"
#include "BLEAdDecode.h"

#define DEBUG_BT_HOME_DECODE

static const char* MODULE_PREFIX = "BLEAdDecode";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode BTHome packet
/// @param event BLE discovery event
/// @param fields BLE advertisement fields
/// @return true if the packet was successfully decoded
bool BLEAdDecode::decodeAdEvent(struct ble_gap_event *event, struct ble_hs_adv_fields& fields)
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
    String logString = "decodeAdEvent " + Raft::formatMACAddr(event->disc.addr.val, ":", true) + " " + hexStr + " ";

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
                    logString += "decodeAdEvent Flags " + String(flags, 16) + " " + flagString + ", ";
                }
                break;
            }
            case 0x02: 
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07: logString += "decodeAdEvent ServiceClass list " + String(adType, 16) + ", "; break;
            case 0x08:
            case 0x09:
            {
                // Local name
                if (len > 1)
                {
                    String name((const char*)(pData+2), len-1);
                    logString += "decodeAdEvent Local name " + name + ", ";
                }
                else
                {
                    logString += "decodeAdEvent Local name TOO SHORT,";
                }
                break;
            }
            case 0x0a: logString += "decodeAdEvent TxPowerLevel " + String(pData[2], 16) + ", "; break;
            case 0x0d: logString += "decodeAdEvent ClassOfDevice, "; break;
            case 0x0e: logString += "decodeAdEvent SimplePairingHashC, "; break;
            case 0x0f: logString += "decodeAdEvent SimplePairingRandomizerR, "; break;
            case 0x10: logString += "decodeAdEvent DeviceID, "; break;
            case 0x12: logString += "decodeAdEvent SecurityManagerOOBFlags, "; break;
            case 0x13: logString += "decodeAdEvent SlaveConnectionIntervalRange, "; break;
            case 0x15: logString += "decodeAdEvent ServiceSolicitationUUIDs, "; break;
            case 0x16:
            {
                // 16-bit ServiceData UUID
                if (len >= 3)
                {
                    uint16_t uuid = (pData[3] << 8) | pData[2];
                    switch(uuid)
                    {
                        case 0xFEED:
                        {
                            logString += "decodeAdEvent ServiceData 16-bit UUID Tile Inc len " + String(len-3) + ", ";
                            break;
                        }
                        case 0xFCD2:
                        {
                            logString += "decodeAdEvent ServiceData 16-bit UUID BTHome Alterco Robotics len " + String(len-3) + ", ";
                            BLEAdDecode::decodeBtHome(pData+4, len-3);
                            break;
                        }
                        case 0xFCF1:
                        {
                            logString += "decodeAdEvent ServiceData 16-bit UUID Google len " + String(len-3) + ", ";
                            break;
                        }
                        default:
                        {
                            logString += "decodeAdEvent ServiceData 16-bit UUID " + String(uuid, 16) + " len " + String(len-3) + ", ";
                            break;
                        }
                    }
                }
                break;
            }
            case 0x17: logString += "decodeAdEvent PublicTargetAddress, "; break;
            case 0x18: logString += "decodeAdEvent RandomTargetAddress, "; break;
            case 0x19: logString += "decodeAdEvent Appearance len " + String(len) + ", "; break;
            case 0x1a: logString += "decodeAdEvent AdvertisingInterval, "; break;
            case 0x1b: logString += "decodeAdEvent LEBluetoothDeviceAddress, "; break;
            case 0x1c: logString += "decodeAdEvent LERole, "; break;
            case 0x20: logString += "decodeAdEvent ServiceData 32bit UUID, "; break;
            case 0x21: logString += "decodeAdEvent ServiceData 128bit UUID, "; break;
            case 0x22: logString += "decodeAdEvent LE Secure Connections Confirmation Value, "; break;
            case 0x23: logString += "decodeAdEvent LE Secure Connections Random Value, "; break;
            case 0x24: logString += "decodeAdEvent URI, "; break;
            case 0x25: logString += "decodeAdEvent IndoorPositioning, "; break;
            case 0x26: logString += "decodeAdEvent TransportDiscoveryData, "; break;
            case 0x27: logString += "decodeAdEvent LE Supported Features, "; break;
            case 0x28: logString += "decodeAdEvent ChannelMapUpdateIndication, "; break;
            case 0x29: logString += "decodeAdEvent PB-ADV, "; break;
            case 0x2a: logString += "decodeAdEvent MeshMessage, "; break;
            case 0x2b: logString += "decodeAdEvent MeshBeacon, "; break;
            case 0x3d: logString += "decodeAdEvent 3DInformationData, "; break;
            case 0xff: logString += "decodeAdEvent ManufacturerSpecificData len " + String(len) + ", "; break;
            default:
            {
                logString += "decodeAdEvent adType " + String(adType, 16) + " len " + String(len) + ", ";
                break;
            }
        }

        // Move to next packet
        pData += len + 1;
        remainingDataLen -= (len + 1);
    }

    // Log the accumulated messages
    LOG_I(MODULE_PREFIX, "%s", logString.c_str());

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode BTHome 
/// @param pBtHomeData BTHome data
/// @param btHomeDataLen BTHome data length
/// @return true if the packet was successfully decoded
bool BLEAdDecode::decodeBtHome(const uint8_t* pBtHomeData, int btHomeDataLen)
{
#ifdef DEBUG_BT_HOME_DECODE
    String logStr;
    Raft::getHexStrFromBytes(pBtHomeData, btHomeDataLen, logStr);
#endif

    // Check for minimum length
    if (btHomeDataLen < 1)
        return false;
    
    // Get the BTHome Device Information
    uint8_t btHomeDeviceInfo = pBtHomeData[0];
    bool isEncrypted = (btHomeDeviceInfo & 0x01) != 0;
    bool isTriggerBased = (btHomeDeviceInfo & 0x04) != 0;
    uint8_t btHomeVersion = (btHomeDeviceInfo >> 5) & 0x07;

#ifdef DEBUG_BT_HOME_DECODE
    logStr += " DevInfo " + String(isEncrypted ? "ENC " : "NOENC ") + String(isTriggerBased ? "TRIG " : "NO_TRIG ") + "Ver " + String(btHomeVersion);
#endif

    // Start of variable data
    const uint8_t* pVarData = pBtHomeData + 1;
    int varDataLen = btHomeDataLen - 1;

    // Decode the fields
    while (varDataLen > 0)
    {
        switch(pVarData[0])
        {
            case 0x00: // Packet ID
            {
                if (varDataLen >= 2)
                {
                    uint8_t packetID = pVarData[1];
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
                    uint8_t battLevel = pVarData[1];
#ifdef DEBUG_BT_HOME_DECODE
                    logStr += " Battery " + String(battLevel);
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
                    int16_t tempX100 = (pVarData[2] << 8) | pVarData[1];
#ifdef DEBUG_BT_HOME_DECODE
                    logStr += " Temp " + String(tempX100/100.0, 2);
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
                    uint32_t illum = (pVarData[2] << 16) | (pVarData[1] << 8) | pVarData[0];
#ifdef DEBUG_BT_HOME_DECODE
                    logStr += " Illuminance " + String(illum/100.0);
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
                    uint8_t motion = pVarData[1];
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

#ifdef DEBUG_BT_HOME_DECODE
    LOG_I(MODULE_PREFIX, "decodeBtHome %s", logStr.c_str());
#endif

    return false;
}
