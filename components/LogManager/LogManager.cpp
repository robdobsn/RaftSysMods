/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Log Manager
// Handles logging to different destinations
//
// Rob Dobson 2021-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "LogManager.h"
#include "SysManager.h"
#include "LoggerCore.h"
#include "LoggerPapertrail.h"
#include "LoggerRaftRemote.h"

// #define DEBUG_LOG_MANAGER

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param pModuleName Module name
/// @param sysConfig System configuration
LogManager::LogManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
void LogManager::setup()
{
    // Clear existing loggers
    loggerCore.clearLoggers();

    // Get config
    std::vector<String> logDests;
    configGetArrayElems("logDests", logDests);
    for (const RaftJson logDestConfig : logDests)
    {
        // Get type
        bool isEnabled = logDestConfig.getBool("enable", false);
#ifdef DEBUG_LOG_MANAGER
        LOG_I(MODULE_PREFIX, "en %s logDest %s", 
                isEnabled ? "YES" : "NO",
                logDestConfig.getConfigString().c_str());
#endif
        if (!isEnabled)
            continue;
        String logDestType = logDestConfig.getString("type", "");
        if (logDestType.equalsIgnoreCase("RaftRemote"))
        {
            // Construct raft remote logger
            LoggerRaftRemote *pLogger = new LoggerRaftRemote(logDestConfig, getSystemName(), getSystemUniqueString());
            loggerCore.addLogger(pLogger);
            // LOG_I(MODULE_PREFIX, "Added RaftRemote logger");
        }
        else if (logDestType.equalsIgnoreCase("Papertrail"))
        {
            // Construct papertrail logger
            LoggerPapertrail *pLogger = new LoggerPapertrail(logDestConfig, getSystemName(), getSystemUniqueString());
            loggerCore.addLogger(pLogger);
            // LOG_I(MODULE_PREFIX, "Added Papertrail logger");
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop
void LogManager::loop()
{
    loggerCore.loop();
}