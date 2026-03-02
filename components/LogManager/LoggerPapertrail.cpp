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

    // Create ring buffer for deferred sending
    _ringBuf = xRingbufferCreate(RING_BUF_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (!_ringBuf)
    {
        ESP_LOGE(MODULE_PREFIX, "Failed to create ring buffer");
    }
}

LoggerPapertrail::~LoggerPapertrail()
{
    if (_ringBuf)
    {
        vRingbufferDelete(_ringBuf);
        _ringBuf = nullptr;
    }
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
    // Check level and not paused
    if ((level > _level) || _isPaused)
        return;

    // Check ring buffer is valid
    if (!_ringBuf)
        return;

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

    // Push to ring buffer (non-blocking, 0 tick timeout)
    // If the buffer is full the message is silently dropped - acceptable back-pressure
    xRingbufferSend(_ringBuf, logMsg.c_str(), logMsg.length() + 1, 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop - called from main task context, safe to do network I/O
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoggerPapertrail::loop()
{
    // Check ring buffer is valid
    if (!_ringBuf)
        return;

    // Check socket is ready
    ip_addr_t hostIPAddr;
    if (!checkSocket(hostIPAddr))
        return;

    // Form the destination address once
    struct sockaddr_in destAddr;
    destAddr.sin_addr.s_addr = hostIPAddr.u_addr.ip4.addr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(atoi(_port.c_str()));

    // Drain ring buffer and send each message
    static const uint32_t MAX_MSGS_PER_LOOP = 10;
    for (uint32_t i = 0; i < MAX_MSGS_PER_LOOP; i++)
    {
        size_t itemSize = 0;
        void* pItem = xRingbufferReceive(_ringBuf, &itemSize, 0);
        if (!pItem)
            break;

        // Send to papertrail using UDP socket
        int ret = sendto(_socketFd, pItem, itemSize - 1, 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (ret < 0)
        {
            if (Raft::isTimeout(millis(), _internalLoggingFailedErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                ESP_LOGI(MODULE_PREFIX, "log failed: %d errno %d socketFd %d ipAddr %s msgLen %d",
                            ret, errno, _socketFd, ipaddr_ntoa(&hostIPAddr), (int)(itemSize - 1));
                _internalLoggingFailedErrorLastTime = millis();
            }
        }

        // Return item to ring buffer
        vRingbufferReturnItem(_ringBuf, pItem);
    }
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
                        _hostname.c_str(), _port.c_str(), getLevelStr(_level), _sysName.c_str(), _socketFd, 
                        fcntl(_socketFd, F_GETFL, 0));
    return true;
}

