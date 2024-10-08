/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommandSocket
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include "RestAPIEndpointManager.h"
#include "RaftSysMod.h"

// #define USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
#ifdef USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
#include "AsyncTCP.h"
#endif

class CommsChannelMsg;

class CommandSocket : public RaftSysMod
{
public:
    // Constructor/destructor
    CommandSocket(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~CommandSocket();

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new CommandSocket(pModuleName, sysConfig);
    }
    
protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager &endpointManager) override final;

    // Add comms channels
    virtual void addCommsChannels(CommsCoreIF& commsCoreIF) override final;

private:
    // Helpers
    void applySetup();
    void begin();
    void end();
    bool sendMsg(CommsChannelMsg& msg);

    // Vars
    bool _isEnabled;
    uint32_t _port;
    bool _begun;
    String _protocol;

    // EndpointID used to identify this message channel to the CommsCoreIF
    uint32_t _commsChannelID;

#ifdef USE_ASYNC_SOCKET_FOR_COMMAND_SOCKET
    // Socket server
    AsyncServer *_pTcpServer;

    // Client connected
    void addClient(AsyncClient* pClient);
    
    // Remove client
    void removeFromClientList(AsyncClient* pClient);

    // Client list
    static const uint32_t MAX_CLIENTS = 2;
    std::list<AsyncClient*> _clientList;

    // Mutex controlling access to clients
    SemaphoreHandle_t _clientMutex;
#endif

    // // Handles websocket events
    // static void webSocketCallback(uint8_t num, WEBSOCKET_TYPE_t type, const char* msg, uint64_t len);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "CmdSock";
};
