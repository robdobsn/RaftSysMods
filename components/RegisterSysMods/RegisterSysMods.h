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
#include "BLEManager.h"
#include "CommandFile.h"
#include "CommandSerial.h"
#include "CommandSocket.h"
#include "ESPOTAUpdate.h"
#include "FileManager.h"
#include "LogManager.h"
#include "MQTTManager.h"
#include "NetworkManager.h"
#include "SerialConsole.h"
#include "StatePublisher.h"
#include "WebServer.h"

// Check if networking is enabled
#if defined(CONFIG_ESP_WIFI_ENABLED) || defined(CONFIG_ETH_USE_ESP32_EMAC) || defined(CONFIG_ETH_USE_SPI_ETHERNET) || defined(CONFIG_ETH_USE_OPENETH) || defined(CONFIG_ETH_USE_RMII_ETHERNET)
#define NETWORKING_IS_ENABLED
#endif

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
        sysManager.registerSysMod("CommandSerial", CommandSerial::create);
        
        // Command Socket
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("CommandSocket", CommandSocket::create, false, "NetMan");
#endif

        // ESPOTAUpdate
        sysManager.registerSysMod("ESPOTAUpdate", ESPOTAUpdate::create);
        
        // FileManager
        sysManager.registerSysMod("FileManager", FileManager::create);

        // LogManager
        sysManager.registerSysMod("LogManager", LogManager::create);

        // MQTTManager
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("MQTTMan", MQTTManager::create, false, "NetMan");
#endif

        // NetworkManager
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("NetMan", NetworkManager::create);
#endif

        // Serial Console
        sysManager.registerSysMod("SerialConsole", SerialConsole::create);
        
        // StatePublisher
        sysManager.registerSysMod("Publish", StatePublisher::create);
    }
}
