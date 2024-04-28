/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Log Manager
// Handles logging to different destinations
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "LogManager.h"

class RaftJsonIF;
class RestAPIEndpointManager;
class APISourceInfo;
class LogManager : public RaftSysMod
{
public:
    LogManager(const char* pModuleName, RaftJsonIF& sysConfig);

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new LogManager(pModuleName, sysConfig);
    }
    
protected:
    // Setup
    virtual void setup() override final;
};
