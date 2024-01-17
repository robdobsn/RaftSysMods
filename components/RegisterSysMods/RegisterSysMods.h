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
        sysManager.registerSysMod("BLEMan", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new BLEManager(pSysModName, sysConfig); },
                10, false
        );
#endif

        // Command File
        sysManager.registerSysMod("CommandFile", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new CommandFile(pSysModName, sysConfig); },
                10, false
        );

        // Command Serial
        sysManager.registerSysMod("CommandSerial", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new CommandSerial(pSysModName, sysConfig); },
                10, false
        );
        
        // Command Socket
        sysManager.registerSysMod("CommandSocket", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new CommandSocket(pSysModName, sysConfig); },
                10, false
        );

        // ESPOTAUpdate
        sysManager.registerSysMod("ESPOTAUpdate", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new ESPOTAUpdate(pSysModName, sysConfig); },
                10, false
        );
        
        // FileManager
        sysManager.registerSysMod("FileManager", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new FileManager(pSysModName, sysConfig); },
                2, true
        );

        // LogManager
        sysManager.registerSysMod("LogManager", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new LogManager(pSysModName, sysConfig); },
                5, true
        );

        // MQTTManager
        sysManager.registerSysMod("MQTTMan", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new MQTTManager(pSysModName, sysConfig); },
                10, false
        );

        // NetworkManager
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("NetMan", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new NetworkManager(pSysModName, sysConfig); },
                1, true
        );
#endif

        // Serial Console
        sysManager.registerSysMod("SerialConsole", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new SerialConsole(pSysModName, sysConfig); },
                10, true
        );
        
        // StatePublisher
        sysManager.registerSysMod("Publish", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new StatePublisher(pSysModName, sysConfig); },
                10, false
        );

        // WebServer
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("WebServer", 
                [](const char* pSysModName, RaftJsonIF& sysConfig) -> SysModBase* { return new WebServer(pSysModName, sysConfig); },
                7, false
        );
#endif
    }
}
