/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Decode BLE Advertisement
// https://bthome.io/format/
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <stdint.h>
#include "sdkconfig.h"
#undef min
#undef max
#include "host/ble_gap.h"
#undef min
#undef max

namespace BLEAdDecode
{
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Decode ad packet
    /// @param event BLE discovery event
    /// @param fields BLE advertisement fields
    /// @return true if the packet was successfully decoded
    bool decodeAdEvent(struct ble_gap_event *event, struct ble_hs_adv_fields& fields);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Decode BTHome 
    /// @param pBtHomeData BTHome data
    /// @param btHomeDataLen BTHome data length
    /// @return true if the packet was successfully decoded
    bool decodeBtHome(const uint8_t* pBtHomeData, int btHomeDataLen);
}
