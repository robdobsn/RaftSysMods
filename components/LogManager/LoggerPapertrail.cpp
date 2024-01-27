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
#include "ESPUtils.h"
#include "RaftUtils.h"

// Debug
#define DEBUG_LOGGER_PAPERTRAIL
// #define DEBUG_LOGGER_PAPERTRAIL_DETAIL

// Log prefix
static const char *MODULE_PREFIX = "LogPapertrail";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

LoggerPapertrail::LoggerPapertrail(const RaftJsonIF& logDestConfig, const String& systemName, const String& systemUniqueString)
    : LoggerBase(logDestConfig)
{
    // Get config
    _host = logDestConfig.getString("host", "");
    memset(&_hostAddrInfo, 0, sizeof(struct addrinfo));
    _dnsLookupDone = false;
    _port = logDestConfig.getLong("port", 0);
    _sysName = logDestConfig.getString("sysName", systemName.c_str());
    _sysName += "_" + systemUniqueString;
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

    // Check if we're connected
    if (!networkSystem.isIPConnected())
        return;

    // Check if DNS lookup done
    if (!_dnsLookupDone)
    {
        // Do DNS lookup
        struct addrinfo hints;
        memset(&hints,0,sizeof(hints));
        hints.ai_family=AF_INET;
        hints.ai_socktype=SOCK_DGRAM;
        hints.ai_flags=0;
        struct addrinfo *addrResult;
        if (getaddrinfo(_host.c_str(), _port.c_str(), &hints, &addrResult) != 0)
        {
            if (Raft::isTimeout(millis(), _internalDNSResolveErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                ESP_LOGE(MODULE_PREFIX, "log failed to resolve host %s", _host.c_str());
                _internalDNSResolveErrorLastTime = millis();
            }
            return;
        }
#ifdef DEBUG_LOGGER_PAPERTRAIL_DETAIL
        ESP_LOGI(MODULE_PREFIX, "log resolved host %s to %d.%d.%d.%d", _host.c_str(), 
                    addrResult->ai_addr->sa_data[0], addrResult->ai_addr->sa_data[1],
                    addrResult->ai_addr->sa_data[2], addrResult->ai_addr->sa_data[3]);
#endif
        _hostAddrInfo = *addrResult;
        _dnsLookupDone = true;

        // Create UDP socket
#ifdef DEBUG_LOGGER_PAPERTRAIL_DETAIL
        ESP_LOGI(MODULE_PREFIX, "log create udp socket");
#endif
        _socketFd = socket(_hostAddrInfo.ai_family, _hostAddrInfo.ai_socktype, _hostAddrInfo.ai_protocol);
        if (_socketFd < 0)
        {
            if (Raft::isTimeout(millis(), _internalSocketCreateErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                ESP_LOGE(MODULE_PREFIX, "log create udp socket failed: %d errno: %d", _socketFd, errno);
                _internalSocketCreateErrorLastTime = millis();
            }
            return;
        }

        // Set non-blocking
        fcntl(_socketFd, F_SETFL, fcntl(_socketFd, F_GETFL, 0) | O_NONBLOCK);

        // Debug
#ifdef DEBUG_LOGGER_PAPERTRAIL
        ESP_LOGI(MODULE_PREFIX, "log hostIP %s port %s level %s sysName %s socketFd %d socketFlags %04x", 
                            _host.c_str(), _port.c_str(), getLevelStr(), _sysName.c_str(), _socketFd, 
                            fcntl(_socketFd, F_GETFL, 0));
#endif
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
            return;
        }
    }

    // Format message
    String logMsg = "<22>" + _sysName + ": " + msg;

    // Send to papertrail using UDP socket
    int ret = sendto(_socketFd, logMsg.c_str(), logMsg.length(), 0, _hostAddrInfo.ai_addr, _hostAddrInfo.ai_addrlen);
    if (ret < 0)
    {
        if (Raft::isTimeout(millis(), _internalLoggingFailedErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGE(MODULE_PREFIX, "log failed: %d errno %d", ret, errno);
            _internalLoggingFailedErrorLastTime = millis();
        }
        return;
    }
}
