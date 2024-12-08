/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Decode BLE Advertisement
// https://bthome.io/format/
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

#define PLACE_IN_FLASH_STORE
#ifdef PLACE_IN_FLASH_STORE
#include "esp_attr.h"
#define PLACE_IN_SECTION(x) __attribute__((section(x)))
#else
#define PLACE_IN_SECTION(x)
#endif


// Define the struct to hold length and factor
struct BTHomeSensorType {
    int8_t len;    // Length of the data in bytes
    int8_t factor; // Factor as an encoded value (1=1, 2=0.1, 3=0.01, etc.)
};

// BTHome sensor types
static const BTHomeSensorType BTHOME_SENSOR_TYPES[] PLACE_IN_SECTION(".rodata") = {
    {1, 1},  // 0x00 Packet ID
    {1, 1},  // 0x01 Battery
    {2, 3},  // 0x02 Temperature (0.01 factor)
    {2, 3},  // 0x03 Humidity (0.01 factor)
    {3, 3},  // 0x04 Pressure (0.01 factor)
    {3, 3},  // 0x05 Illuminance (0.01 factor)
    {2, 3},  // 0x06 Mass (kg) (0.01 factor)
    {2, 3},  // 0x07 Mass (lb) (0.01 factor)
    {2, 3},  // 0x08 Dewpoint (0.01 factor)
    {1, 1},  // 0x09 Count
    {3, 4},  // 0x0A Energy (0.001 factor)
    {3, 3},  // 0x0B Power (0.01 factor)
    {2, 4},  // 0x0C Voltage (0.001 factor)
    {2, 1},  // 0x0D PM2.5
    {2, 1},  // 0x0E PM10
    {1, 1},  // 0x0F Generic Boolean
    {1, 1},  // 0x10 Power
    {1, 1},  // 0x11 Opening
    {2, 1},  // 0x12 CO2
    {2, 1},  // 0x13 TVOC
    {2, 3},  // 0x14 Moisture (0.01 factor)
    {1, 1},  // 0x15 Battery
    {1, 1},  // 0x16 Battery Charging
    {1, 1},  // 0x17 Carbon Monoxide
    {1, 1},  // 0x18 Cold
    {1, 1},  // 0x19 Connectivity
    {1, 1},  // 0x1A Door
    {1, 1},  // 0x1B Garage Door
    {1, 1},  // 0x1C Gas
    {1, 1},  // 0x1D Heat
    {1, 1},  // 0x1E Light
    {1, 1},  // 0x1F Lock
    {1, 1},  // 0x20 Moisture
    {1, 1},  // 0x21 Motion
    {1, 1},  // 0x22 Moving
    {1, 1},  // 0x23 Occupancy
    {1, 1},  // 0x24 Plug
    {1, 1},  // 0x25 Presence
    {1, 1},  // 0x26 Problem
    {1, 1},  // 0x27 Running
    {1, 1},  // 0x28 Safety
    {1, 1},  // 0x29 Smoke
    {1, 1},  // 0x2A Sound
    {1, 1},  // 0x2B Tamper
    {1, 1},  // 0x2C Vibration
    {1, 1},  // 0x2D Window
    {1, 1},  // 0x2E Humidity
    {1, 1},  // 0x2F Moisture
    {-1, 0}, // 0x30 Unused
    {-1, 0}, // 0x31 Unused
    {-1, 0}, // 0x32 Unused
    {-1, 0}, // 0x33 Unused
    {-1, 0}, // 0x34 Unused
    {-1, 0}, // 0x35 Unused
    {-1, 0}, // 0x36 Unused
    {-1, 0}, // 0x37 Unused
    {-1, 0}, // 0x38 Unused
    {-1, 0}, // 0x39 Unused
    {3, 4},  // 0x3A Event Button
    {-1, 0}, // 0x3B Unused
    {2, 4},  // 0x3C Event Dimmer
    {2, 1},  // 0x3D Count (16-bit, factor 1)
    {4, 1},  // 0x3E Count (32-bit, factor 1)
    {2, 2},  // 0x3F Rotation (0.1 factor)
    {2, 1},  // 0x40 Distance (mm)
    {2, 2},  // 0x41 Distance (m, 0.1 factor)
    {3, 4},  // 0x42 Duration (0.001 factor)
    {2, 4},  // 0x43 Current (0.001 factor)
    {2, 3},  // 0x44 Speed (0.01 factor)
    {2, 2},  // 0x45 Temperature (0.1 factor)
    {1, 10}, // 0x46 UV Index (special case 0.35)
    {2, 2},  // 0x47 Volume
    {2, 1},  // 0x48 Volume (mL)
    {2, 4},  // 0x49 Volume Flow Rate (0.001 factor)
    {2, 4},  // 0x4A Voltage (2 bytes, 0.1 factor)
    {3, 4},  // 0x4B Gas (3 bytes, 0.001 factor)
    {4, 4},  // 0x4C Gas (4 bytes, 0.001 factor)
    {4, 4},  // 0x4D Energy (4 bytes, 0.001 factor)
    {4, 4},  // 0x4E Volume (4 bytes, 0.001 factor)
    {4, 4},  // 0x4F Water (4 bytes, 0.001 factor)
    {4, 1},  // 0x50 Timestamp (4 bytes, no factor)
    {2, 4},  // 0x51 Acceleration (2 bytes, 0.001 factor)
    {2, 4},  // 0x52 Gyroscope (2 bytes, 0.001 factor)
    {-1, 0}, // 0x53 Text (variable length, no factor)
    {-1, 0}, // 0x54 Raw (variable length, no factor)
    {4, 4},  // 0x55 Volume Storage (4 bytes, 0.001 factor)
    {2, 1},  // 0x56 Conductivity (2 bytes, no factor)
    {1, 1},  // 0x57 Temperature (1 byte, 1 factor)
    {1, 10}, // 0x58 Temperature (1 byte, 0.35 factor)
    {1, 1},  // 0x59 Count (1 byte, signed)
    {2, 1},  // 0x5A Count (2 bytes, signed)
    {4, 1},  // 0x5B Count (4 bytes, signed)
    {4, 3},  // 0x5C Power (4 bytes, 0.01 factor, signed)
    {2, 4},  // 0x5D Current (2 bytes, 0.001 factor, signed)
};

static const int BTHOME_SENSOR_TYPE_COUNT = sizeof(BTHOME_SENSOR_TYPES) / sizeof(BTHomeSensorType);
