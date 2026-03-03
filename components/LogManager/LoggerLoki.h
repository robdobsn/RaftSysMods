/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Grafana Loki logger
// Sends log messages to Loki's standard /loki/api/v1/push JSON API
// Uses FreeRTOS ring buffer for thread-safe ingestion from any context
// Network I/O performed exclusively from loop() on the main task
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "LoggerBase.h"
#include "RaftArduino.h"
#include "DNSResolver.h"
#include "freertos/ringbuf.h"
#include "esp_http_client.h"

class RaftJsonIF;

class LoggerLoki : public LoggerBase
{
public:
    LoggerLoki(const RaftJsonIF& logDestConfig, const String& systemName,
               const String& systemUniqueString);
    virtual ~LoggerLoki();
    virtual void log(esp_log_level_t level, const char* tag, const char* msg) override final;
    virtual void loop() override;

private:
    // Config
    String _hostname;
    uint16_t _port = 3100;
    String _path;
    String _sysName;
    DNSResolver _dnsResolver;

    // Authentication
    enum AuthType { AUTH_NONE, AUTH_BASIC, AUTH_BEARER };
    AuthType _authType = AUTH_NONE;
    String _authHeader;  // Pre-formatted "Basic xxx" or "Bearer xxx"

    // HTTP client handle (persistent connection with keep-alive)
    esp_http_client_handle_t _httpClient = nullptr;

    // Ring buffer for deferred sending from loop()
    // log() pushes items here; loop() drains and POSTs as Loki JSON.
    // This avoids calling network I/O from arbitrary task contexts.
    // Item format: [1 byte: esp_log_level_t][N bytes: "tag: msg\0"]
    RingbufHandle_t _ringBuf = nullptr;
    static const uint32_t RING_BUF_SIZE = 16384;

    // Max size of a single log item (level byte + message)
    static const uint32_t MAX_LOG_ITEM_SIZE = 1024;

    // Batching
    static const uint32_t MAX_MSGS_PER_BATCH = 20;

    // Rate limiting (same as Papertrail)
    uint32_t _logWindowStartMs = 0;
    uint32_t _logWindowCount = 0;
    static const uint32_t LOG_WINDOW_SIZE_MS = 60000;
    static const uint32_t LOG_WINDOW_MAX_COUNT = 60;

    // Backoff on send failure to avoid repeatedly blocking the main loop
    uint32_t _sendFailBackoffStartMs = 0;
    bool _inSendBackoff = false;
    static const uint32_t SEND_FAIL_BACKOFF_MS = 30000;

    // Internal error throttling
    uint32_t _internalErrorLastTimeMs = 0;
    static const uint32_t INTERNAL_ERROR_LOG_MIN_GAP_MS = 10000;

    // Helpers
    bool ensureHttpClient();
    void destroyHttpClient();
    bool sendBatch(const String& payload, const char* highestLevelStr);
    void formatBatchPayload(String& outPayload, const char*& highestLevelStr,
                            void* items[], size_t itemSizes[],
                            uint32_t count);
    static void jsonEscapeAppend(String& out, const char* str);
    static const char* lokiLevelStr(esp_log_level_t level);
    static bool isTimeValid();

    // Log prefix
    static constexpr const char* MODULE_PREFIX = "LogLoki";
};
