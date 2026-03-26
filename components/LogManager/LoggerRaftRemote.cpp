/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Raft Remote logger
// TCP server-based logger using ring buffer for thread safety
//
// Rob Dobson 2025-2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "LoggerRaftRemote.h"
#include "Logger.h"
#include "NetworkSystem.h"
#include "RaftUtils.h"

// Debug
// #define DEBUG_LOGGER_RAFTREMOTE
// #define DEBUG_LOGGER_RAFTREMOTE_DETAIL
// #define DEBUG_LOGGER_RAFTREMOTE_SOCKET
// #define DEBUG_LOGGER_RAFTREMOTE_LOOP

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param logDestConfig Configuration for the logger
/// @param systemName Name of the system
/// @param systemUniqueString Unique string for the system
LoggerRaftRemote::LoggerRaftRemote(const RaftJsonIF& logDestConfig, const String& systemName, 
                const String& systemUniqueString, RestAPIEndpointManager* pRestAPIEndpointManager)
    : LoggerBase(logDestConfig), _pRestAPIEndpointManager(pRestAPIEndpointManager)
{
    // Get config
    _port = logDestConfig.getLong("port", 0);
    _sysName = logDestConfig.getString("sysName", systemName.c_str());
    _sysName += "_" + systemUniqueString;

    // Ring buffer is created lazily when a client connects (saves 16KB when idle)
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
LoggerRaftRemote::~LoggerRaftRemote()
{
    if (_ringBuf)
    {
        vRingbufferDelete(_ringBuf);
        _ringBuf = nullptr;
    }
    if (_clientSocketFd >= 0) 
        close(_clientSocketFd);
    if (_serverSocketFd >= 0)
        close(_serverSocketFd);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Log a message - called from any task context, must be safe and non-blocking
/// @param level Log level
/// @param tag Tag
/// @param msg Message
void LOGGING_FUNCTION_DECORATOR LoggerRaftRemote::log(esp_log_level_t level, const char *tag, const char* msg)
{
    // Check level and not paused
    if ((level > _level) || _isPaused)
        return;

    // Check ring buffer is valid
    if (!_ringBuf)
        return;

    // Rate limiting - start of log window?
    if (Raft::isTimeout(millis(), _logWindowStartMs, _logWindowSizeMs))
    {
        _logWindowStartMs = millis();
        _logWindowCount = 1;
    }
    else
    {
        _logWindowCount++;
        if (_logWindowCount >= _logWindowMaxCount)
            return;
    }

    // msg already contains the full formatted log line (e.g. "I (12345) Tag: message\n")
    // so store it as-is without adding level or tag prefix
    size_t msgLen = strlen(msg);

    // Strip trailing newline if present (we add our own when sending)
    if (msgLen > 0 && msg[msgLen - 1] == '\n')
        msgLen--;

    // Truncate if too large for ring buffer
    if (msgLen >= MAX_LOG_ITEM_SIZE)
        msgLen = MAX_LOG_ITEM_SIZE - 1;

    // Build item on stack: [msg\0]
    char item[MAX_LOG_ITEM_SIZE];
    memcpy(item, msg, msgLen);
    size_t itemLen = msgLen + 1;
    item[msgLen] = '\0';

    // Push to ring buffer (non-blocking, 0 tick timeout)
    // If the buffer is full the message is silently dropped
    xRingbufferSend(_ringBuf, item, itemLen, 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop - called from main task context, safe to do network I/O
void LoggerRaftRemote::loop()
{
#ifdef DEBUG_LOGGER_RAFTREMOTE_LOOP
    // Check if time for debug
    if (Raft::isTimeout(millis(), _debugLastMs, DEBUG_INTERVAL_MS))
    {
        _debugLastMs = millis();
        ESP_LOGI(MODULE_PREFIX, "loop clientFd %d serverFd %d", (int)_clientSocketFd, (int)_serverSocketFd);
    }
#endif

    // Check if time to check server and connection
    if (Raft::isTimeout(millis(), _connCheckLastMs, CONN_CHECK_INTERVAL_MS))
    {
        _connCheckLastMs = millis();

        // Check if server socket needs starting
        if (_serverSocketFd < 0)
        {
            startServer();
        }
        else
        {
            // Check connection and handle incoming data
            checkConnection();
            if (_clientSocketFd >= 0)
            {
                handleIncomingData();
            }
        }
    }

    // Back off after send failure to avoid repeatedly blocking main loop
    if (_inSendBackoff)
    {
        if (!Raft::isTimeout(millis(), _sendFailBackoffStartMs, SEND_FAIL_BACKOFF_MS))
            return;
        _inSendBackoff = false;
    }

    // Drain ring buffer and send to connected client
    if (!_ringBuf || _clientSocketFd < 0)
        return;

    for (uint32_t i = 0; i < MAX_MSGS_PER_LOOP; i++)
    {
        size_t itemSize = 0;
        void* pItem = xRingbufferReceive(_ringBuf, &itemSize, 0);
        if (!pItem)
            break;

        // Item is the raw formatted log line (no level byte prefix)
        const char* msg = (const char*)pItem;

        // Send as "message\n"
        char sendBuf[MAX_LOG_ITEM_SIZE + 2];
        int sendLen = snprintf(sendBuf, sizeof(sendBuf), "%s\n", msg);
        if (sendLen > 0)
        {
            int ret = send(_clientSocketFd, sendBuf, sendLen, 0);
            if (ret < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    if (Raft::isTimeout(millis(), _internalErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
                    {
                        ESP_LOGE(MODULE_PREFIX, "send failed errno %d", errno);
                        _internalErrorLastTimeMs = millis();
                    }
                    vRingbufferReturnItem(_ringBuf, pItem);
                    closeClientAndFreeRingBuf();
                    _sendFailBackoffStartMs = millis();
                    _inSendBackoff = true;
                    break;
                }
                // EAGAIN - socket busy, skip this message
            }
        }

        // Return item to ring buffer
        vRingbufferReturnItem(_ringBuf, pItem);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Start server
bool LoggerRaftRemote::startServer()
{
    // Check if we're connected to IP
    if (!networkSystem.isIPConnected())
        return false;

    // Initialize server socket
    _serverSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocketFd < 0) 
    {
        ESP_LOGE(MODULE_PREFIX, "startServer FAIL create socket errno %d", errno);
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(_serverSocketFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGE(MODULE_PREFIX, "Failed to set socket options: errno %d", errno);
        close(_serverSocketFd);
        _serverSocketFd = -1;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(_serverSocketFd, F_GETFL, 0);
    if (flags < 0) {
        ESP_LOGE(MODULE_PREFIX, "Failed to get socket flags: errno %d", errno);
        close(_serverSocketFd);
        _serverSocketFd = -1;
        return false;
    }
    if (fcntl(_serverSocketFd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ESP_LOGE(MODULE_PREFIX, "Failed to set non-blocking mode: errno %d", errno);
        close(_serverSocketFd);
        _serverSocketFd = -1;
        return false;
    }

    // Bind socket
    struct sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(atoi(_port.c_str()));
    if (bind(_serverSocketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) 
    {
        ESP_LOGE(MODULE_PREFIX, "startServer FAIL bind errno %d", errno);
        close(_serverSocketFd);
        _serverSocketFd = -1;
        return false;
    }

    // Listen for incoming connections
    if (listen(_serverSocketFd, 1) < 0) 
    {
        ESP_LOGE(MODULE_PREFIX, "startServer FAIL listen errno %d", errno);
        close(_serverSocketFd);
        _serverSocketFd = -1;
    }

    ESP_LOGI(MODULE_PREFIX, "startServer OK port %s", _port.c_str());
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check connection
bool LoggerRaftRemote::checkConnection()
{
    // Check if we're connected to IP
    if (!networkSystem.isIPConnected())
        return false;

    // Check if client already connected
    if (_clientSocketFd >= 0)
        return true;

    // Check for connections
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    _clientSocketFd = accept(_serverSocketFd, (struct sockaddr*)&clientAddr, &addrLen);
    if (_clientSocketFd < 0) 
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK) 
        {
            ESP_LOGE(MODULE_PREFIX, "checkConnection FAIL accept errno %d", errno);
        }
        return false;
    }

    // Get socket flags
    int flags = fcntl(_clientSocketFd, F_GETFL, 0);
    if (flags < 0)
    {
        ESP_LOGE(MODULE_PREFIX, "checkConnection FAIL fcntl flags %d errno %d", flags, errno);
        close(_clientSocketFd);
        _clientSocketFd = -1;
        return false;
    }

    // Set non-blocking
    if (fcntl(_clientSocketFd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        ESP_LOGE(MODULE_PREFIX, "checkConnection FAIL non-block flags %d errno: %d", flags, errno);
        close(_clientSocketFd);
        _clientSocketFd = -1;
        return false;
    }

    // Create ring buffer now that we have a client to send to
    if (!_ringBuf)
    {
        _ringBuf = xRingbufferCreate(_ringBufSize, RINGBUF_TYPE_NOSPLIT);
        if (!_ringBuf)
        {
            ESP_LOGE(MODULE_PREFIX, "Failed to create ring buffer");
            close(_clientSocketFd);
            _clientSocketFd = -1;
            return false;
        }
    }

#ifdef DEBUG_LOGGER_RAFTREMOTE_SOCKET
    ESP_LOGI(MODULE_PREFIX, "checkConnection OK handle %d", _clientSocketFd);
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle incoming data from the client
void LoggerRaftRemote::handleIncomingData()
{
    // Buffer for reading data
    char buf[300];
    int bytesRead = recv(_clientSocketFd, buf, sizeof(buf) - 1, 0);

    // Check if data received
    if (bytesRead > 0)
    {
        // Null-terminate the received data
        buf[bytesRead] = '\0';

        // Remove final newline if present
        if (buf[bytesRead - 1] == '\n')
        {
            buf[bytesRead - 1] = '\0';
            bytesRead--;
        }

#ifdef DEBUG_LOGGER_RAFTREMOTE_DETAIL
        String debugStr;
        Raft::hexDump((const uint8_t*)buf, bytesRead, debugStr);
        ESP_LOGI(MODULE_PREFIX, "handleIncomingData bytesRead %d debugStr %s", bytesRead, debugStr.c_str());
#endif

        // Handle as a single line
        String curLine = buf;
        curLine.trim();

        // Handle the command
        String retStr;
        if (_pRestAPIEndpointManager)
        {
            _pRestAPIEndpointManager->handleApiRequest(curLine.c_str(), retStr, 
                    APISourceInfo(RestAPIEndpointManager::CHANNEL_ID_REMOTE_CONTROL));
        }

        // Send response
        sendResponse(retStr + "\n");
    }
    else if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        // Handle connection error
        if (Raft::isTimeout(millis(), _internalErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            ESP_LOGE(MODULE_PREFIX, "handleIncomingData FAIL recv errno %d", errno);
            _internalErrorLastTimeMs = millis();
        }
        closeClientAndFreeRingBuf();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send a response to the client
/// @param response Response string
void LoggerRaftRemote::sendResponse(const String& response)
{
    if (_clientSocketFd >= 0)
    {
        int ret = send(_clientSocketFd, response.c_str(), response.length(), 0);
        if (ret < 0)
        {
            if (Raft::isTimeout(millis(), _internalErrorLastTimeMs, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                ESP_LOGE(MODULE_PREFIX, "sendResponse FAIL errno %d", errno);
                _internalErrorLastTimeMs = millis();
            }
            closeClientAndFreeRingBuf();
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Close client socket and free ring buffer
void LoggerRaftRemote::closeClientAndFreeRingBuf()
{
    if (_clientSocketFd >= 0)
    {
        close(_clientSocketFd);
        _clientSocketFd = -1;
    }
    if (_ringBuf)
    {
        vRingbufferDelete(_ringBuf);
        _ringBuf = nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Convert esp_log_level_t to level string
const char* LoggerRaftRemote::levelStr(esp_log_level_t level)
{
    switch (level)
    {
        case ESP_LOG_ERROR:   return "E";
        case ESP_LOG_WARN:    return "W";
        case ESP_LOG_INFO:    return "I";
        case ESP_LOG_DEBUG:   return "D";
        case ESP_LOG_VERBOSE: return "V";
        default:              return "?";
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Disconnect any active client connection
/// @return true if a connection was disconnected
bool LoggerRaftRemote::disconnect()
{
    if (_clientSocketFd < 0)
        return false;
    closeClientAndFreeRingBuf();
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Configure logger parameters at runtime
/// @param config JSON containing optional keys: level, maxcount, windowms, bufsize
/// @return true if any parameter was applied
bool LoggerRaftRemote::configure(const RaftJsonIF& config)
{
    bool changed = LoggerBase::configure(config);

    // Rate limit max count
    long maxCount = config.getLong("maxcount", -1);
    if (maxCount > 0)
    {
        _logWindowMaxCount = (uint32_t)maxCount;
        changed = true;
    }

    // Rate limit window
    long windowMs = config.getLong("windowms", -1);
    if (windowMs > 0)
    {
        _logWindowSizeMs = (uint32_t)windowMs;
        changed = true;
    }

    // Ring buffer size (takes effect on next client connect)
    long bufSize = config.getLong("bufsize", -1);
    if (bufSize > 0)
    {
        _ringBufSize = (uint32_t)bufSize;
        changed = true;
    }

    return changed;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get extended JSON status
/// @return JSON string with type-specific details
String LoggerRaftRemote::getExtendedStatusJSON() const
{
    String json = "{";
    json += "\"type\":\"" + _loggerType + "\"";
    json += ",\"level\":\"";
    json += getLevelStr(_level);
    json += "\"";
    json += ",\"enabled\":";
    json += _isPaused ? "0" : "1";
    json += ",\"connected\":";
    json += (_clientSocketFd >= 0) ? "1" : "0";
    json += ",\"port\":";
    json += _port;
    json += ",\"maxcount\":";
    json += String(_logWindowMaxCount);
    json += ",\"windowms\":";
    json += String(_logWindowSizeMs);
    json += ",\"bufsize\":";
    json += String(_ringBufSize);
    json += "}";
    return json;
}
