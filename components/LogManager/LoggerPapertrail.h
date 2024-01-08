/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Papertrail logger
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "LoggerBase.h"
#include "RaftArduino.h"
#include "sys/types.h"
#include "sys/socket.h"
#include "netdb.h"

class RaftJsonIF;

class LoggerPapertrail : public LoggerBase
{
public:
    LoggerPapertrail(const RaftJsonIF& logDestConfig, const String& systemUniqueString);
    virtual ~LoggerPapertrail();
    virtual void log(esp_log_level_t level, const char *tag, const char* msg) override final;

private:
    String _host;
    String _port;
    String _sysName;
    struct addrinfo _hostAddrInfo;
    bool _dnsLookupDone = false;
    int _socketFd = -1;

    // Avoid swamping the network
    uint32_t _logWindowStartMs = 0;
    uint32_t _logWindowCount = 0;
    static const uint32_t LOG_WINDOW_SIZE_MS = 60000;
    static const uint32_t LOG_WINDOW_MAX_COUNT = 60;
    uint32_t _logWindowThrottleStartMs = 0;
    static const uint32_t LOG_WINDOW_THROTTLE_BACKOFF_MS = 30000;

    // Avoid logging internal errors too often
    uint32_t _internalDNSResolveErrorLastTime = 0;
    uint32_t _internalSocketCreateErrorLastTime = 0;
    uint32_t _internalLoggingFailedErrorLastTime = 0;
    static const uint32_t INTERNAL_ERROR_LOG_MIN_GAP_MS = 10000;
};
