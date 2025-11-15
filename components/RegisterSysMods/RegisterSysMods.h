/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Register SysMods
// Register all the SysMods with the SysManager
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "sdkconfig.h"
#include "SysManager.h"

// Check if networking is enabled
#if defined(CONFIG_ESP_WIFI_ENABLED) || defined(CONFIG_ETH_USE_ESP32_EMAC) || defined(CONFIG_ETH_USE_SPI_ETHERNET) || defined(CONFIG_ETH_USE_OPENETH) || defined(CONFIG_ETH_USE_RMII_ETHERNET)
#define NETWORKING_IS_ENABLED
#endif

// Include headers conditionally based on what's available
#if CONFIG_BT_ENABLED
#include "BLEManager.h"
#endif
#include "CommandFile.h"
#include "CommandSerial.h"
#ifdef NETWORKING_IS_ENABLED
#include "CommandSocket.h"
#endif
#ifdef ESP_PLATFORM
#include "ESPOTAUpdate.h"
#include "LogManager.h"
#endif
#include "FileManager.h"
#ifdef NETWORKING_IS_ENABLED
#include "MQTTManager.h"
#include "NetworkManager.h"
#endif
#ifdef ESP_PLATFORM
#include "SampleCollectorJSON.h"
#endif
#include "SerialConsole.h"
#include "StatePublisher.h"

namespace RegisterSysMods
{
    void registerSysMods(SysManager& sysManager)
    {

        // BLE
#if CONFIG_BT_ENABLED
        sysManager.registerSysMod("BLEMan", BLEManager::create);
#endif

        // Command File
        sysManager.registerSysMod("CommandFile", CommandFile::create);

        // Command Serial
#ifdef ESP_PLATFORM
        sysManager.registerSysMod("CommandSerial", CommandSerial::create);
#endif
        
        // Command Socket
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("CommandSocket", CommandSocket::create, false, "NetMan");
#endif

        // ESPOTAUpdate
#ifdef ESP_PLATFORM
        sysManager.registerSysMod("ESPOTAUpdate", ESPOTAUpdate::create);
#endif
        
        // FileManager
        sysManager.registerSysMod("FileManager", FileManager::create, true);

        // LogManager
#ifdef ESP_PLATFORM
        sysManager.registerSysMod("LogManager", LogManager::create);
#endif

        // MQTTManager
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("MQTTMan", MQTTManager::create, false, "NetMan");
#endif

        // NetworkManager
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("NetMan", NetworkManager::create, true);
#endif

        // Sample collector JSON
#ifdef ESP_PLATFORM
        sysManager.registerSysMod("SamplesJSON", SampleCollectorJSON::create);
#endif

        // Serial Console
        sysManager.registerSysMod("SerialConsole", SerialConsole::create);
        
        // StatePublisher
        sysManager.registerSysMod("Publish", StatePublisher::create);
    }
}
