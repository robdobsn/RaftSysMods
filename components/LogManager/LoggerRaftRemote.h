/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Raft Remote logger
// TCP server-based logger: clients connect to receive log stream
// Uses FreeRTOS ring buffer for thread-safe ingestion from any context
// Network I/O performed exclusively from loop() on the main task
//
// Rob Dobson 2025-2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "LoggerBase.h"
#include "RaftArduino.h"
#include "freertos/ringbuf.h"
#include "sys/types.h"
#include "sys/socket.h"
#include "netdb.h"
#include "RestAPIEndpointManager.h"

class RaftJsonIF;

class LoggerRaftRemote : public LoggerBase
{
public:
    LoggerRaftRemote(const RaftJsonIF& logDestConfig, const String& systemName, 
            const String& systemUniqueString, RestAPIEndpointManager* pRestAPIEndpointManager);
    virtual ~LoggerRaftRemote();
    virtual void log(esp_log_level_t level, const char *tag, const char* msg) override final;
    virtual void loop() override final;
    virtual bool disconnect() override;
    virtual bool configure(const RaftJsonIF& config) override;
    virtual String getExtendedStatusJSON() const override;

private:
    // Config
    String _port;
    String _sysName;

    // Sockets
    int _serverSocketFd = -1;
    int _clientSocketFd = -1;

    // Rest API endpoint manager
    RestAPIEndpointManager* _pRestAPIEndpointManager = nullptr;

    // Ring buffer for deferred sending from loop()
    // log() pushes items here; loop() drains and sends via TCP.
    // This avoids calling send() from arbitrary thread contexts.
    // Item format: [1 byte: esp_log_level_t][N bytes: "tag: msg\0"]
    RingbufHandle_t _ringBuf = nullptr;
    uint32_t _ringBufSize = 16384;

    // Max size of a single log item (level byte + message)
    static const uint32_t MAX_LOG_ITEM_SIZE = 1024;

    // Max messages to drain per loop iteration
    static const uint32_t MAX_MSGS_PER_LOOP = 10;

    // Avoid swamping the network
    uint32_t _logWindowStartMs = 0;
    uint32_t _logWindowCount = 0;
    uint32_t _logWindowSizeMs = 60000;
    uint32_t _logWindowMaxCount = 60;

    // Backoff on send failure to avoid repeatedly blocking the main loop
    uint32_t _sendFailBackoffStartMs = 0;
    bool _inSendBackoff = false;
    static const uint32_t SEND_FAIL_BACKOFF_MS = 30000;

    // Internal error throttling
    uint32_t _internalErrorLastTimeMs = 0;
    static const uint32_t INTERNAL_ERROR_LOG_MIN_GAP_MS = 10000;

    // Connection checking
    uint32_t _connCheckLastMs = 0;
    static const uint32_t CONN_CHECK_INTERVAL_MS = 500;

    // Debug
    uint32_t _debugLastMs = 0;
    static const uint32_t DEBUG_INTERVAL_MS = 10000;

    // Helpers
    bool startServer();
    bool checkConnection();
    void handleIncomingData();
    void sendResponse(const String& response);
    void closeClientAndFreeRingBuf();
    static const char* levelStr(esp_log_level_t level);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "LogRaftRemote";
};
