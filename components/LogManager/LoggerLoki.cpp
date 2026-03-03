/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Grafana Loki logger
// Sends log messages to Loki's standard /loki/api/v1/push JSON API
// JSON payload with streams/values format, device and job labels
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "LoggerLoki.h"
#include "Logger.h"
#include "NetworkSystem.h"
#include "RaftUtils.h"
#include "RaftJson.h"
#include "cencode.h"
#include <sys/time.h>

// Debug
// #define DEBUG_LOGGER_LOKI
// #define DEBUG_LOGGER_LOKI_PAYLOAD
// #define DEBUG_LOGGER_LOKI_HTTP

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

LoggerLoki::LoggerLoki(const RaftJsonIF& logDestConfig, const String& systemName,
                       const String& systemUniqueString)
    : LoggerBase(logDestConfig)
{
    // Get config
    _hostname = logDestConfig.getString("host", "");
    _port = logDestConfig.getLong("port", 3100);
    _path = logDestConfig.getString("path", "/loki/api/v1/push");
    _sysName = logDestConfig.getString("sysName", systemName.c_str());
    _sysName += "_" + systemUniqueString;

    // Setup DNS resolver
    _dnsResolver.setHostname(_hostname.c_str());

    // Authentication
    String authTypeStr = logDestConfig.getString("authType", "none");
    if (authTypeStr.equalsIgnoreCase("basic"))
    {
        _authType = AUTH_BASIC;
        String user = logDestConfig.getString("authUser", "");
        String key = logDestConfig.getString("authKey", "");
        String credentials = user + ":" + key;

        // Base64 encode using libb64
        base64_encodestate state;
        base64_init_encodestate_nonewlines(&state);
        size_t expectedLen = base64_encode_expected_len_nonewlines(credentials.length());
        char* encoded = new char[expectedLen + 2];
        if (encoded)
        {
            int len = base64_encode_block(credentials.c_str(), credentials.length(), encoded, &state);
            len += base64_encode_blockend(encoded + len, &state);
            encoded[len] = '\0';
            _authHeader = "Basic ";
            _authHeader += encoded;
            delete[] encoded;
        }
    }
    else if (authTypeStr.equalsIgnoreCase("bearer"))
    {
        _authType = AUTH_BEARER;
        _authHeader = "Bearer " + logDestConfig.getString("authKey", "");
    }

    // Create ring buffer for deferred sending
    _ringBuf = xRingbufferCreate(RING_BUF_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (!_ringBuf)
    {
        ESP_LOGE(MODULE_PREFIX, "Failed to create ring buffer");
    }

    ESP_LOGI(MODULE_PREFIX, "created host %s port %d path %s sysName %s auth %s",
             _hostname.c_str(), _port, _path.c_str(), _sysName.c_str(),
             _authType == AUTH_BASIC ? "basic" : (_authType == AUTH_BEARER ? "bearer" : "none"));
}

LoggerLoki::~LoggerLoki()
{
    destroyHttpClient();
    if (_ringBuf)
    {
        vRingbufferDelete(_ringBuf);
        _ringBuf = nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logging - called from any task context, must be safe and non-blocking
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LOGGING_FUNCTION_DECORATOR LoggerLoki::log(esp_log_level_t level, const char* tag, const char* msg)
{
    // Check level and not paused
    if ((level > _level) || _isPaused)
        return;

    // Check ring buffer is valid
    if (!_ringBuf)
        return;

    // Rate limiting - start of log window?
    if (Raft::isTimeout(millis(), _logWindowStartMs, LOG_WINDOW_SIZE_MS))
    {
        _logWindowStartMs = millis();
        _logWindowCount = 1;
    }
    else
    {
        _logWindowCount++;
        if (_logWindowCount >= LOG_WINDOW_MAX_COUNT)
            return;
    }

    // Calculate item size: 1 byte level + tag + ": " + msg + \0
    size_t tagLen = strlen(tag);
    size_t msgLen = strlen(msg);
    size_t itemLen = 1 + tagLen + 2 + msgLen + 1;

    // Truncate if too large for ring buffer
    if (itemLen > MAX_LOG_ITEM_SIZE)
    {
        msgLen = MAX_LOG_ITEM_SIZE - 1 - tagLen - 2 - 1;
        itemLen = MAX_LOG_ITEM_SIZE;
    }

    // Build item on stack: [level_byte][tag: msg\0]
    char item[MAX_LOG_ITEM_SIZE];
    item[0] = (char)level;
    memcpy(item + 1, tag, tagLen);
    item[1 + tagLen] = ':';
    item[2 + tagLen] = ' ';
    memcpy(item + 3 + tagLen, msg, msgLen);
    item[itemLen - 1] = '\0';

    // Push to ring buffer (non-blocking, 0 tick timeout)
    // If the buffer is full the message is silently dropped
    xRingbufferSend(_ringBuf, item, itemLen, 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop - called from main task context, safe to do network I/O
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoggerLoki::loop()
{
    if (!_ringBuf)
        return;

    // Back off after send failure to avoid repeatedly blocking main loop
    if (_inSendBackoff)
    {
        if (!Raft::isTimeout(millis(), _sendFailBackoffStartMs, SEND_FAIL_BACKOFF_MS))
            return;
        _inSendBackoff = false;
    }

    // Don't send until we have valid wall-clock time (SNTP synced)
    // Messages accumulate safely in the ring buffer until then
    if (!isTimeValid())
        return;

    // Drain messages from ring buffer
    void* items[MAX_MSGS_PER_BATCH];
    size_t itemSizes[MAX_MSGS_PER_BATCH];
    uint32_t count = 0;

    for (uint32_t i = 0; i < MAX_MSGS_PER_BATCH; i++)
    {
        size_t itemSize = 0;
        void* pItem = xRingbufferReceive(_ringBuf, &itemSize, 0);
        if (!pItem)
            break;

        items[count] = pItem;
        itemSizes[count] = itemSize;
        count++;
    }

    // Nothing to send?
    if (count == 0)
        return;

    // Ensure HTTP client is ready
    if (!ensureHttpClient())
    {
        // Can't send - return items to ring buffer (messages are lost)
        for (uint32_t i = 0; i < count; i++)
            vRingbufferReturnItem(_ringBuf, items[i]);
        return;
    }

    // Format JSON payload and determine highest severity level
    String payload;
    const char* highestLevelStr = nullptr;
    formatBatchPayload(payload, highestLevelStr, items, itemSizes, count);

    // Send batch
    bool ok = sendBatch(payload, highestLevelStr);
    if (!ok)
    {
        // Back off before retrying
        _sendFailBackoffStartMs = millis();
        _inSendBackoff = true;
    }

    // Return all items to ring buffer
    for (uint32_t i = 0; i < count; i++)
        vRingbufferReturnItem(_ringBuf, items[i]);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format batch payload as Loki JSON (streams/values format)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoggerLoki::formatBatchPayload(String& outPayload, const char*& highestLevelStr,
                                     void* items[], size_t itemSizes[],
                                     uint32_t count)
{
    // Track highest severity (lowest enum value = most severe: ERROR=1, WARN=2, etc.)
    esp_log_level_t highestLevel = ESP_LOG_VERBOSE;

    // Get current Unix timestamp in nanoseconds (Loki format)
    // gettimeofday() returns wall-clock time set by SNTP
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t timestampNs = ((uint64_t)tv.tv_sec * 1000000000ULL) + ((uint64_t)tv.tv_usec * 1000ULL);

    // Build JSON payload in Loki streams format
    outPayload = "{\"streams\":[{\"stream\":{";
    outPayload += "\"job\":\"esp32\",";
    outPayload += "\"device\":\"";
    jsonEscapeAppend(outPayload, _sysName.c_str());
    outPayload += "\"},\"values\":[";

    for (uint32_t i = 0; i < count; i++)
    {
        if (i > 0)
            outPayload += ",";

        // Extract level and message from ring buffer item
        const char* itemData = (const char*)items[i];
        esp_log_level_t level = (esp_log_level_t)itemData[0];
        const char* msg = itemData + 1;  // "tag: message"

        // Track highest severity
        if (level < highestLevel)
            highestLevel = level;

        // Build timestamp string
        char tsStr[24];
        snprintf(tsStr, sizeof(tsStr), "%llu", (unsigned long long)timestampNs);

        // Add ["timestamp", "[LEVEL] tag: message"]
        outPayload += "[\"";
        outPayload += tsStr;
        outPayload += "\",\"[";
        outPayload += lokiLevelStr(level);
        outPayload += "] ";
        jsonEscapeAppend(outPayload, msg);
        outPayload += "\"]";

        // Increment by 1ms for ordering within batch
        timestampNs += 1000000;
    }

    outPayload += "]}]}";
    highestLevelStr = lokiLevelStr(highestLevel);

#ifdef DEBUG_LOGGER_LOKI_PAYLOAD
    ESP_LOGI(MODULE_PREFIX, "payload (%d msgs, %d bytes, level %s): %.200s%s",
             count, outPayload.length(), highestLevelStr, outPayload.c_str(),
             outPayload.length() > 200 ? "..." : "");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ensure HTTP client is created and ready
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LoggerLoki::ensureHttpClient()
{
    // Check if we're connected to IP
    if (!networkSystem.isIPConnected())
        return false;

    // If client already exists, reuse it
    if (_httpClient)
        return true;

    // Resolve hostname
    ip_addr_t hostIPAddr;
    if (!_dnsResolver.getIPAddr(hostIPAddr))
    {
        if (Raft::isTimeout(millis(), _internalErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGI(MODULE_PREFIX, "DNS not resolved for %s", _hostname.c_str());
            _internalErrorLastTimeMs = millis();
        }
        return false;
    }

    // Build URL using resolved IP
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s",
             ipaddr_ntoa(&hostIPAddr), _port, _path.c_str());

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 30;
    config.keep_alive_interval = 5;

    _httpClient = esp_http_client_init(&config);
    if (!_httpClient)
    {
        if (Raft::isTimeout(millis(), _internalErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGE(MODULE_PREFIX, "Failed to init HTTP client");
            _internalErrorLastTimeMs = millis();
        }
        return false;
    }

    // Set persistent headers
    esp_http_client_set_header(_httpClient, "Content-Type", "application/json");
    if (_authType != AUTH_NONE)
        esp_http_client_set_header(_httpClient, "Authorization", _authHeader.c_str());

#ifdef DEBUG_LOGGER_LOKI_HTTP
    ESP_LOGI(MODULE_PREFIX, "HTTP client created url %s deviceId %s", url, _sysName.c_str());
#endif

    ESP_LOGI(MODULE_PREFIX, "HTTP client OK host %s port %d path %s",
             _hostname.c_str(), _port, _path.c_str());
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destroy HTTP client
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoggerLoki::destroyHttpClient()
{
    if (_httpClient)
    {
        esp_http_client_cleanup(_httpClient);
        _httpClient = nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send batch via HTTP POST
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LoggerLoki::sendBatch(const String& payload, const char* highestLevelStr)
{
    esp_http_client_set_post_field(_httpClient, payload.c_str(), payload.length());

#ifdef DEBUG_LOGGER_LOKI_HTTP
    ESP_LOGI(MODULE_PREFIX, "sendBatch POST body (%d bytes):", (int)payload.length());
    // Print full body in chunks of 200 chars to avoid truncation
    const char* p = payload.c_str();
    int remaining = payload.length();
    int chunk = 0;
    while (remaining > 0)
    {
        int len = remaining > 200 ? 200 : remaining;
        ESP_LOGI(MODULE_PREFIX, "  body[%d]: %.*s", chunk, len, p);
        p += len;
        remaining -= len;
        chunk++;
    }
#endif

    esp_err_t err = esp_http_client_perform(_httpClient);
    if (err != ESP_OK)
    {
        if (Raft::isTimeout(millis(), _internalErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGE(MODULE_PREFIX, "HTTP POST failed: %s", esp_err_to_name(err));
            _internalErrorLastTimeMs = millis();
        }
        // Destroy client so it gets recreated next time
        destroyHttpClient();
        return false;
    }

    int statusCode = esp_http_client_get_status_code(_httpClient);

#ifdef DEBUG_LOGGER_LOKI_HTTP
    ESP_LOGI(MODULE_PREFIX, "HTTP POST response: %d (payload %d bytes)", statusCode, (int)payload.length());
#endif

    if (statusCode != 204)  // Loki returns 204 No Content on success
    {
        if (Raft::isTimeout(millis(), _internalErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGW(MODULE_PREFIX, "Loki endpoint returned HTTP %d", statusCode);
            _internalErrorLastTimeMs = millis();
        }
        // Destroy client on error status to force reconnect
        destroyHttpClient();
        return false;
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert esp_log_level_t to level string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* LoggerLoki::lokiLevelStr(esp_log_level_t level)
{
    switch (level)
    {
        case ESP_LOG_ERROR:   return "ERROR";
        case ESP_LOG_WARN:    return "WARN";
        case ESP_LOG_INFO:    return "INFO";
        case ESP_LOG_DEBUG:   return "DEBUG";
        case ESP_LOG_VERBOSE: return "VERBOSE";
        default:              return "UNKNOWN";
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Escape a string for embedding in a JSON string value
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoggerLoki::jsonEscapeAppend(String& out, const char* str)
{
    while (*str)
    {
        char c = *str++;
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                }
                else
                {
                    out += c;
                }
                break;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if system time is valid (SNTP has synced)
// Returns true if time is after Jan 1 2024 (epoch 1704067200)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LoggerLoki::isTimeValid()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    // Jan 1 2024 00:00:00 UTC = 1704067200
    return tv.tv_sec > 1704067200;
}