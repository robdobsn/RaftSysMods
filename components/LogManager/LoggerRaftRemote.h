/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Raft Remote logger
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "LoggerBase.h"
#include "RaftArduino.h"
#include "sys/types.h"
#include "sys/socket.h"
#include "netdb.h"

class RaftJsonIF;

class LoggerRaftRemote : public LoggerBase
{
public:
    LoggerRaftRemote(const RaftJsonIF& logDestConfig, const String& systemName, const String& systemUniqueString);
    virtual ~LoggerRaftRemote();
    virtual void log(esp_log_level_t level, const char *tag, const char* msg) override final;
    virtual void loop() override final;

private:
    // Config
    String _port;
    String _sysName;

    // Sockets
    int _serverSocketFd = -1;
    int _clientSocketFd = -1;

    // Recursion detector
    bool _inLog = false;

    // Avoid swamping the network
    uint32_t _logWindowStartMs = 0;
    uint32_t _logWindowCount = 0;
    static const uint32_t LOG_WINDOW_SIZE_MS = 60000;
    static const uint32_t LOG_WINDOW_MAX_COUNT = 60;
    uint32_t _logWindowThrottleStartMs = 0;
    static const uint32_t LOG_WINDOW_THROTTLE_BACKOFF_MS = 30000;

    // Avoid logging internal errors too often
    uint32_t _internalDNSResolveErrorLastTimeMs = 0;
    uint32_t _internalSocketCreateErrorLastTimeMs = 0;
    uint32_t _internalLoggingFailedErrorLastTime = 0;
    static const uint32_t INTERNAL_ERROR_LOG_MIN_GAP_MS = 10000;

    // Connection checking
    uint32_t _connCheckLastMs = 0;
    static const uint32_t CONN_CHECK_INTERVAL_MS = 500;

    // Count of blocked messages
    mutable uint32_t _connBusyCount = 0;

    // Debug
    uint32_t _debugLastMs = 0;
    static const uint32_t DEBUG_INTERVAL_MS = 10000;

    // Helpers
    bool startServer();
    bool checkConnection();

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "LogRaftRemote";
};
