/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommandSocket
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "CommandSocket.h"
#include "RaftUtils.h"
#include "RestAPIEndpointManager.h"
#include "NetworkSystem.h"
#include "CommsChannelSettings.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommandSocket::CommandSocket(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
    // Config variables
    _isEnabled = false;
    _port = 0;
    _begun = false;

#ifdef USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
    _pTcpServer = NULL;
    _clientMutex = xSemaphoreCreateMutex();
#endif

    // ChannelID
    _commsChannelID = CommsCoreIF::CHANNEL_ID_UNDEFINED;
}

CommandSocket::~CommandSocket()
{
#ifdef USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
    if (_pTcpServer)
    {
        end();
        delete _pTcpServer;
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSocket::setup()
{
    // Apply config
    applySetup();
}

void CommandSocket::applySetup()
{
    // Enable
    _isEnabled = configGetBool("enable", false);

    // Port
    _port = configGetLong("socketPort", 24);

    // Protocol
    _protocol = configGetString("protocol", "RICSerial");

    // Check if already setup
#ifdef USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
    if (_pTcpServer)
    {
        end();
        delete _pTcpServer;
        _pTcpServer = NULL;
    }

    // Setup server
    if (_isEnabled)
    {
        _pTcpServer = new AsyncServer(_port);
        _pTcpServer->onClient([this](void *s, AsyncClient *c) {
            if (c == NULL)
                return;
            c->setRxTimeout(3);
            this->addClient(c);

            // Verbose
            LOG_V(MODULE_PREFIX, "received socket client");

            // // TODO remove after finished with only
            // c->close(true);
            // c->free();
            // delete c;

            // AsyncWebServerRequest *r = new AsyncWebServerRequest((AsyncWebServer *)s, c);
            // if (r == NULL)
            // {
            //     c->close(true);
            //     c->free();
            //     delete c;
            // }
        }, this);
    }
#endif

    // Debug
    LOG_I(MODULE_PREFIX, "setup isEnabled %s TCP port %d", 
            _isEnabled ? "YES" : "NO", _port);

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop (called frequently)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSocket::loop()
{
    // Check if WiFi is connected and begin if so
    if ((!_begun) && networkSystem.isIPConnected())
    {
        begin();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSocket::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Comms channels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSocket::addCommsChannels(CommsCoreIF &commsCore)
{
    // Comms channel
    static const CommsChannelSettings commsChannelSettings;

    // Register as a message channel
    _commsChannelID = commsCore.registerChannel(_protocol.c_str(),
            modName(),
            modName(),
            std::bind(&CommandSocket::sendMsg, this, std::placeholders::_1),
            [this](uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn) {
                return true;
            },
            &commsChannelSettings);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Begin
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSocket::begin()
{
#ifdef USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
    if (_pTcpServer && !_begun)
    {
        _pTcpServer->setNoDelay(true);
        _pTcpServer->begin();
        _begun = true;

        // Debug
        LOG_I(MODULE_PREFIX, "has started on port %d", _port);
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// End
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSocket::end()
{
#ifdef USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
    if (!_pTcpServer)
        return;
    _pTcpServer->end();
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Client Handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
void CommandSocket::addClient(AsyncClient* pClient)
{
    // Add to client list if enough space
    if (_clientList.size() < MAX_CLIENTS)
    {
        pClient->onError([](void *r, AsyncClient* pClient, int8_t error)
        {
            // TODO - handle
            LOG_I(MODULE_PREFIX, "onError");
            // handleError(error);
        }, this);
        pClient->onAck([](void *r, AsyncClient* pClient, size_t len, uint32_t time)
        {
            // TODO - handle
            LOG_I(MODULE_PREFIX, "onAck");
            // handleAck(len, time); 
        }, this);
        pClient->onDisconnect([this](void *r, AsyncClient* pClient)
        {
            // TODO - handle
            LOG_I(MODULE_PREFIX, "onDisconnect");
            // handleDisconnect();
            delete pClient;
            this->removeFromClientList(pClient);
        }, this);
        pClient->onTimeout([](void *r, AsyncClient* pClient, uint32_t time)
        {
            // TODO - handle
            LOG_I(MODULE_PREFIX, "onTimeout");
            // handleTimeout(time);
        }, this);
        pClient->onData([this](void *r, AsyncClient* pClient, void *buf, size_t len)
        {
            // Received data
            uint8_t* pRxData = (uint8_t*)buf;

            // Debug
            // TODO - _DEBUG_COMMAND_SOCKET_ON_DATA
            // char outBuf[400];
            // strcpy(outBuf, "");
            // char tmpBuf[10];
            // for (int i = 0; i < len; i++)
            // {
            //     sprintf(tmpBuf, "%02x ", pRxData[i]);
            //     strlcat(outBuf, tmpBuf, sizeof(outBuf));
            // }
            // LOG_I(MODULE_PREFIX, "onData RX len %d %s", len, outBuf);

            // Send the message to the CommsCoreIF
            if (getCommsCore())
                getCommsCore()->inboundHandleMsg(this->_commsChannelID, pRxData, len);

            // Handle the data
            // handleData(buf, len);
        }, this);
        pClient->onPoll([](void *r, AsyncClient* pClient)
        {
            // TODO - handle
            LOG_I(MODULE_PREFIX, "onPoll");
            // handlePoll(); 
        }, this);

        // Take mutex
        if (xSemaphoreTake(_clientMutex, portMAX_DELAY) == pdTRUE)
        {
            // Add to list of clients
            _clientList.push_back(pClient);
            // Return the mutex
            xSemaphoreGive(_clientMutex);
        }
    }
    else
    {
        // TODO remove after finished with only
        pClient->close(true);
        pClient->free();
        delete pClient;
    }
}

void CommandSocket::removeFromClientList(AsyncClient* pClient)
{
    // Take mutex
    if (xSemaphoreTake(_clientMutex, portMAX_DELAY) == pdTRUE)
    {
        // Remove from list of clients
        _clientList.remove(pClient);
        // Return the mutex
        xSemaphoreGive(_clientMutex);
    }
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message over socket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommandSocket::sendMsg(CommsChannelMsg& msg)
{
    // LOG_D(MODULE_PREFIX, "sendBLEMsg channelID %d, msgType %s msgNum %d, len %d",
    //         msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen());
    return true;
}
