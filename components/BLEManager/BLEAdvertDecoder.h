/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Decode BLE Advertisement
// https://bthome.io/format/
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include "RaftCore.h"
#include <list>
#include <stdint.h>
#include "sdkconfig.h"
#undef min
#undef max
#include "host/ble_gap.h"
#undef min
#undef max

class RaftBusDevicesIF;

class BLEAdvertDecoder
{
public:

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Decode ad packet
    /// @param event BLE discovery event
    /// @param fields BLE advertisement fields
    /// @param pBusDevicesIF pointer to bus devices interface
    /// @return true if the packet was successfully decoded
    bool decodeAdEvent(struct ble_gap_event *event, struct ble_hs_adv_fields& fields, RaftBusDevicesIF* pBusDevicesIF);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Decode BTHome 
    /// @param bleAddr BLE address
    /// @param pBtHomeData BTHome data
    /// @param btHomeDataLen BTHome data length
    /// @param pBusDevicesIF pointer to bus devices interface
    /// @return true if the packet was successfully decoded
    bool decodeBtHome(ble_addr_t bleAddr, const uint8_t* pBtHomeData, int btHomeDataLen, RaftBusDevicesIF* pBusDevicesIF);

private:
    static constexpr const char* MODULE_PREFIX = "BLEAdvertDecoder";

};

