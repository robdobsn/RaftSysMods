/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Register SysMods
// Register all the SysMods with the SysManager
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "SysManager.h"
#include "BLEManager.h"
#include "CommandFile.h"
#include "CommandSerial.h"

namespace RegisterSysMods
{
    void registerSysMods(SysManager& sysManager)
    {

        // BLE
#if CONFIG_BT_ENABLED
        sysManager.registerSysMod("BLEMan", [](const char* pSysModName) -> SysModBase* { return new BLEManager(pSysModName); });
#endif

        // Command File
        sysManager.registerSysMod("CommandFile", [](const char* pSysModName) -> SysModBase* { return new CommandFile(pSysModName); });

        // Command Serial
        sysManager.registerSysMod("CommandSerial", [](const char* pSysModName) -> SysModBase* { return new CommandSerial(pSysModName); });
        

    }
}
