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
#include "LoggerLoki.h"
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
            LoggerRaftRemote *pLogger = new LoggerRaftRemote(logDestConfig, getSystemName(), getSystemUniqueString(), getRestAPIEndpointManager());
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
        else if (logDestType.equalsIgnoreCase("Loki"))
        {
            // Construct Grafana Loki logger
            LoggerLoki *pLogger = new LoggerLoki(logDestConfig, getSystemName(), getSystemUniqueString());
            loggerCore.addLogger(pLogger);
            // LOG_I(MODULE_PREFIX, "Added Loki logger");
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop
void LogManager::loop()
{
    loggerCore.loop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add REST API endpoints
/// @param endpointManager
void LogManager::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    endpointManager.addEndpoint("log", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&LogManager::apiLog, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "log, log/enable/<name>, log/disable/<name>, log/disconnect/<name>, log/config/<name>?level=&maxcount=&windowms=&bufsize=");
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints log");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API handler for log control
/// @param reqStr REST API request string
/// @param respStr REST API response string (JSON)
/// @param sourceInfo Source of the API request (channel)
RaftRetCode LogManager::apiLog(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Extract parameters
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson nameValuesJson = RaftJson::getJSONFromNVPairs(nameValues, true);

    // Get command (first arg after "log")
    String cmd = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    cmd.trim();

    // No command - return status of all loggers
    if (cmd.length() == 0)
    {
        String loggersJson;
        auto loggers = loggerCore.getLoggers();
        for (auto* pLogger : loggers)
        {
            if (loggersJson.length() > 0)
                loggersJson += ",";
            loggersJson += pLogger->getExtendedStatusJSON();
        }
        String resultJson = "\"loggers\":[" + loggersJson + "]";
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, resultJson.c_str());
    }

    // Commands that require a logger name
    String loggerName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
    loggerName.trim();

    // Find the logger by type name or index
    LoggerBase* pTargetLogger = nullptr;
    auto loggers = loggerCore.getLoggers();
    if (loggerName.length() > 0)
    {
        // Try matching by type name (case-insensitive)
        for (auto* pLogger : loggers)
        {
            if (loggerName.equalsIgnoreCase(pLogger->getLoggerType()))
            {
                pTargetLogger = pLogger;
                break;
            }
        }

        // Try matching by index
        if (!pTargetLogger)
        {
            bool isDigit = true;
            for (uint32_t i = 0; i < loggerName.length(); i++)
            {
                if (!isdigit(loggerName[i]))
                {
                    isDigit = false;
                    break;
                }
            }
            if (isDigit)
            {
                int idx = loggerName.toInt();
                if (idx >= 0 && idx < (int)loggers.size())
                    pTargetLogger = loggers[idx];
            }
        }
    }

    if (!pTargetLogger)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "loggerNotFound");

    // Handle commands
    bool rslt = false;
    if (cmd.equalsIgnoreCase("enable"))
    {
        pTargetLogger->setPaused(false);
        rslt = true;
    }
    else if (cmd.equalsIgnoreCase("disable"))
    {
        pTargetLogger->setPaused(true);
        rslt = true;
    }
    else if (cmd.equalsIgnoreCase("disconnect"))
    {
        rslt = pTargetLogger->disconnect();
    }
    else if (cmd.equalsIgnoreCase("config"))
    {
        rslt = pTargetLogger->configure(nameValuesJson);
    }

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);
}