/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Papertrail logger
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "LoggerPapertrail.h"
#include "Logger.h"
#include "NetworkSystem.h"
#include "RaftUtils.h"

// Debug
// #define DEBUG_LOGGER_PAPERTRAIL
// #define DEBUG_LOGGER_PAPERTRAIL_DETAIL
// #define DEBUG_LOGGER_PAPERTRAIL_SOCKET

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

LoggerPapertrail::LoggerPapertrail(const RaftJsonIF& logDestConfig, const String& systemName, const String& systemUniqueString)
    : LoggerBase(logDestConfig)
{
    // Get config
    _hostname = logDestConfig.getString("host", "");
    _port = logDestConfig.getLong("port", 0);
    _sysName = logDestConfig.getString("sysName", systemName.c_str());
    _sysName += "_" + systemUniqueString;

    // Setup resolver
    _dnsResolver.setHostname(_hostname.c_str());
}

LoggerPapertrail::~LoggerPapertrail()
{
    if (_socketFd >= 0)
    {
        close(_socketFd);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logging
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LOGGING_FUNCTION_DECORATOR LoggerPapertrail::log(esp_log_level_t level, const char *tag, const char* msg)
{
    // Check level
    if (level > _level)
        return;

    // Check for recusion (e.g. if the log function itself logs)
    if (_inLog)
        return;
    _inLog = true;

    // Check socket
    ip_addr_t hostIPAddr;
    if (!checkSocket(hostIPAddr))
    {
        _inLog = false;
        return;
    }

    // Start of log window?
    if (Raft::isTimeout(millis(), _logWindowStartMs, LOG_WINDOW_SIZE_MS))
    {
        _logWindowStartMs = millis();
        _logWindowCount = 1;
    }
    else
    {
        // Count log messages
        _logWindowCount++;
        if (_logWindowCount >= LOG_WINDOW_MAX_COUNT)
        {
            // Discard
            _inLog = false;
            return;
        }
    }

    // Format message
    String logMsg = "<22>" + _sysName + ": " + msg;

    // Form the IP address
    struct sockaddr_in destAddr;
    destAddr.sin_addr.s_addr = hostIPAddr.u_addr.ip4.addr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(atoi(_port.c_str()));

    // Send to papertrail using UDP socket
    int ret = sendto(_socketFd, logMsg.c_str(), logMsg.length(), 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
    if (ret < 0)
    {
        if (Raft::isTimeout(millis(), _internalLoggingFailedErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGI(MODULE_PREFIX, "log failed: %d errno %d socketFd %d ipAddr %s msgLen %d",
                        ret, errno, _socketFd, ipaddr_ntoa(&hostIPAddr), logMsg.length());
            _internalLoggingFailedErrorLastTime = millis();
        }
    }

    // Done
    _inLog = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check socket connected
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LoggerPapertrail::checkSocket(ip_addr_t& hostIPAddr)
{
    // Check if we're connected to IP
    if (!networkSystem.isIPConnected())
        return false;

    // Get IP address
    if (!_dnsResolver.getIPAddr(hostIPAddr))
    {
        if (Raft::isTimeout(millis(), _internalDNSResolveErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGI(MODULE_PREFIX, "checkSocketCreated dns not resolved");
            _internalDNSResolveErrorLastTimeMs = millis();
        }
        return false;
    }

    // Check if socket already connected
    if (_socketFd >= 0)
        return true;

    // Debug
#ifdef DEBUG_LOGGER_PAPERTRAIL_SOCKET
    ESP_LOGI(MODULE_PREFIX, "checkSocketCreated creating udp socket");
#endif

    // Check address family
    if (hostIPAddr.type != IPADDR_TYPE_V4)
    {
        if (Raft::isTimeout(millis(), _internalSocketCreateErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGI(MODULE_PREFIX, "checkSocketCreated invalid address family %d != IPADDR_TYPE_V4", hostIPAddr.type);
            _internalSocketCreateErrorLastTimeMs = millis();
        }
        return false;
    }
    
    // Create UDP socket
    _socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (_socketFd < 0)
    {
#ifdef DEBUG_LOGGER_PAPERTRAIL_SOCKET
        ESP_LOGI(MODULE_PREFIX, "checkSocketCreated create udp socket failed: %d errno: %d", _socketFd, errno);
#endif
        if (Raft::isTimeout(millis(), _internalSocketCreateErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGW(MODULE_PREFIX, "log create udp socket failed: %d errno: %d", _socketFd, errno);
            _internalSocketCreateErrorLastTimeMs = millis();
        }
        return false;
    }

#ifdef DEBUG_LOGGER_PAPERTRAIL_SOCKET
    ESP_LOGI(MODULE_PREFIX, "checkSocketCreated created udp socket - setting non-blocking");
#endif

    // Get socket flags
    int flags = fcntl(_socketFd, F_GETFL, 0);
    if (flags < 0)
    {
        ESP_LOGE(MODULE_PREFIX, "checkSocketCreated get flags failed: %d errno: %d", flags, errno);
        close(_socketFd);
        _socketFd = -1;
        return false;
    }

    // Set non-blocking
    if (fcntl(_socketFd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        ESP_LOGE(MODULE_PREFIX, "checkSocketCreated set non-blocking failed: %d errno: %d", flags, errno);
        close(_socketFd);
        _socketFd = -1;
        return false;
    }

#ifdef DEBUG_LOGGER_PAPERTRAIL_SOCKET
    ESP_LOGI(MODULE_PREFIX, "checkSocketConnected set non-blocking ok");
#endif

    // Debug
    ESP_LOGI(MODULE_PREFIX, "checkSocket OK hostname %s port %s level %s sysName %s socketFd %d socketFlags %04x", 
                        _hostname.c_str(), _port.c_str(), getLevelStr(), _sysName.c_str(), _socketFd, 
                        fcntl(_socketFd, F_GETFL, 0));
    return true;
}

