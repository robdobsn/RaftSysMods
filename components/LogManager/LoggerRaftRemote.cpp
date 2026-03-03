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

    // Push the message string including null terminator to ring buffer
    // (non-blocking, 0 tick timeout - silently dropped if buffer full)
    size_t msgLen = strlen(msg);
    xRingbufferSend(_ringBuf, msg, msgLen + 1, 0);
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

    // Drain ring buffer and send to connected client
    if (!_ringBuf || _clientSocketFd < 0)
        return;

    for (uint32_t i = 0; i < MAX_MSGS_PER_LOOP; i++)
    {
        size_t itemSize = 0;
        void* pItem = xRingbufferReceive(_ringBuf, &itemSize, 0);
        if (!pItem)
            break;

        // Send to connected client (itemSize includes null terminator, send without it)
        int ret = send(_clientSocketFd, pItem, itemSize - 1, 0);
        if (ret < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                ESP_LOGE(MODULE_PREFIX, "send failed errno %d", errno);
                close(_clientSocketFd);
                _clientSocketFd = -1;
                // Free ring buffer since no client is connected
                vRingbufferReturnItem(_ringBuf, pItem);
                vRingbufferDelete(_ringBuf);
                _ringBuf = nullptr;
                break;
            }
            // EAGAIN - socket busy, skip this message
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
        _ringBuf = xRingbufferCreate(RING_BUF_SIZE, RINGBUF_TYPE_NOSPLIT);
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
        ESP_LOGE(MODULE_PREFIX, "handleIncomingData FAIL recv errno %d", errno);
        close(_clientSocketFd);
        _clientSocketFd = -1;
        // Free ring buffer since no client is connected
        if (_ringBuf)
        {
            vRingbufferDelete(_ringBuf);
            _ringBuf = nullptr;
        }
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
            ESP_LOGE(MODULE_PREFIX, "sendResponse FAIL errno %d", errno);
            close(_clientSocketFd);
            _clientSocketFd = -1;
            // Free ring buffer since no client is connected
            if (_ringBuf)
            {
                vRingbufferDelete(_ringBuf);
                _ringBuf = nullptr;
            }
        }
    }
}
