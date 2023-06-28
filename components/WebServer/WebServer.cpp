/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WebServer
//
// Rob Dobson 2012-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef FEATURE_WEB_SERVER_OR_WEB_SOCKETS

#include "WebServer.h"
#include "WebServerResource.h"
#include <Logger.h>
#include <RaftUtils.h>
#include <RestAPIEndpointManager.h>
#include <NetworkSystem.h>
#include <RaftWebServer.h>
#include <CommsCoreIF.h>
#include <CommsChannelMsg.h>
#include <RaftWebHandlerStaticFiles.h>
#include <RaftWebHandlerRestAPI.h>
#include <RaftWebHandlerWS.h>

// #define DEBUG_WEBSERVER_WEBSOCKETS

static const char* MODULE_PREFIX = "WebServer";

// Singleton
WebServer* WebServer::_pThisWebServer = nullptr;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WebServer::WebServer(const char *pModuleName, ConfigBase& defaultConfig, ConfigBase* pGlobalConfig, ConfigBase* pMutableConfig) 
        : SysModBase(pModuleName, defaultConfig, pGlobalConfig, pMutableConfig)
{
    // Singleton
    _pThisWebServer = this;
}

WebServer::~WebServer()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::setup()
{
    // Hook change of config
    configRegisterChangeCallback(std::bind(&WebServer::configChanged, this));

    // Apply config
    applySetup();
}

void WebServer::configChanged()
{
    // Reset config
    LOG_D(MODULE_PREFIX, "configChanged");
    applySetup();
}

void WebServer::applySetup()
{
    // Enable
    _webServerEnabled = configGetBool("enable", false);

    // Port
    _port = configGetLong("webServerPort", RaftWebServerSettings::DEFAULT_HTTP_PORT);

    // Access control allow origin all
    std::vector<String> stdRespHeaders;
    configGetArrayElems("stdRespHeaders", stdRespHeaders);

    // REST API prefix
    _restAPIPrefix = configGetString("apiPrefix", RaftWebServerSettings::DEFAULT_REST_API_PREFIX);

    // File server enable
    bool enableFileServer = configGetBool("fileServer", true);

    // Num connection slots
    uint32_t numConnSlots = configGetLong("numConnSlots", RaftWebServerSettings::DEFAULT_CONN_SLOTS);

    // Websockets
    configGetArrayElems("websockets", _webSocketConfigs);

    // Get Task settings
    UBaseType_t taskCore = configGetLong("taskCore", RaftWebServerSettings::DEFAULT_TASK_CORE);
    BaseType_t taskPriority = configGetLong("taskPriority", RaftWebServerSettings::DEFAULT_TASK_PRIORITY);
    uint32_t taskStackSize = configGetLong("taskStack", RaftWebServerSettings::DEFAULT_TASK_STACK_BYTES);

    // Get server send buffer max length
    uint32_t sendBufferMaxLen = configGetLong("sendMax", RaftWebServerSettings::DEFAULT_SEND_BUFFER_MAX_LEN);

    // Setup server if required
    if (_webServerEnabled)
    {
        // Start server
        if (!_isWebServerSetup)
        {
            RaftWebServerSettings settings(_port, numConnSlots, _webSocketConfigs.size() > 0, 
                    enableFileServer, taskCore, taskPriority, taskStackSize, sendBufferMaxLen,
                    CommsCoreIF::CHANNEL_ID_REST_API, stdRespHeaders, nullptr, nullptr);
            _raftWebServer.setup(settings);
        }
        _isWebServerSetup = true;
    }

#ifdef FEATURE_WEB_SOCKETS
    // Serve websockets
    webSocketSetup();
#endif    
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Begin
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::beginServer()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::service()
{
    // Service
    _raftWebServer.service();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    setupEndpoints();
}

void WebServer::setupEndpoints()
{
    // Handle REST endpoints
    LOG_I(MODULE_PREFIX, "setupEndpoints serverEnabled %s port %d apiPrefix %s", 
            _webServerEnabled ? "Y" : "N", _port, 
            _restAPIPrefix.c_str());
    RaftWebHandlerRestAPI* pHandler = new RaftWebHandlerRestAPI(_restAPIPrefix,
                std::bind(&WebServer::restAPIMatchEndpoint, this, std::placeholders::_1, 
                        std::placeholders::_2, std::placeholders::_3));
    if (!_raftWebServer.addHandler(pHandler))
        delete pHandler;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static Resources
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Add resources to the web server
void WebServer::addStaticResources(const WebServerResource *pResources, int numResources)
{
}

void WebServer::addStaticResource(const WebServerResource *pResource, const char *pAliasPath)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static Files
// 
// Serve static files from the file system
// @param servePaths (comma separated and can include uri=path pairs separated by =)
// @param cacheControl (nullptr or cache control header value eg "no-cache, no-store, must-revalidate")
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::serveStaticFiles(const char* servePaths, const char* cacheControl)
{
    // Handle default serve path
    String servePathsStr = servePaths ? servePaths : "";
    if (servePathsStr.length() == 0)
    {
        servePathsStr = String("/=/") + fileSystem.getDefaultFSRoot();
        servePathsStr += ",/files/local=/local";
        servePathsStr += ",/files/sd=/sd";
    }

    // Handle file systems
    RaftWebHandlerStaticFiles* pHandler = new RaftWebHandlerStaticFiles(servePathsStr.c_str(), cacheControl);
    bool handlerAddOk = _raftWebServer.addHandler(pHandler);
    LOG_I(MODULE_PREFIX, "serveStaticFiles servePaths %s addResult %s", servePathsStr.c_str(), 
                handlerAddOk ? "OK" : "FILE SERVER DISABLED");
    if (!handlerAddOk)
        delete pHandler;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Async Events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::enableServerSideEvents(const String& eventsURL)
{
}

void WebServer::sendServerSideEvent(const char* eventContent, const char* eventGroup)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Web sockets
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::webSocketSetup()
{
    // Comms channel
    static const CommsChannelSettings commsChannelSettings;

    // Add websocket handler
#ifdef DEBUG_WEBSERVER_WEBSOCKETS
    LOG_I(MODULE_PREFIX, "webSocketSetup num websocket configs %d", _webSocketConfigs.size());
#endif
    CommsCoreIF* pCommsCore = getCommsCore();
    if (!pCommsCore)
        return;

    // Create websockets
    for (uint32_t wsIdx = 0; wsIdx < _webSocketConfigs.size(); wsIdx++)        
    {
        // Get config
        ConfigBase jsonConfig = _webSocketConfigs[wsIdx];

        // Setup WebHandler for Websockets    
        RaftWebHandlerWS* pHandler = new RaftWebHandlerWS(jsonConfig, 
                std::bind(&CommsCoreIF::canAcceptInbound, pCommsCore, 
                        std::placeholders::_1),
                std::bind(&CommsCoreIF::handleInboundMessage, pCommsCore, 
                        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
                );
        if (!pHandler)
            continue;

        // Add handler
        if (!_raftWebServer.addHandler(pHandler))
        {
            delete pHandler;
            continue;
        }

        // Register a channel  with the protocol endpoint manager
        // for each possible connection
        uint32_t maxConn = pHandler->getMaxConnections();
        for (uint32_t connIdx = 0; connIdx < maxConn; connIdx++)
        {
            String interfaceName = jsonConfig.getString("pfix", "ws");
            String wsName = interfaceName + "_" + connIdx;
            String protocol = jsonConfig.getString("pcol", "RICSerial");
            uint32_t wsChanID = pCommsCore->registerChannel(
                    protocol.c_str(), 
                    interfaceName.c_str(),
                    wsName.c_str(),
                    [this, pHandler](CommsChannelMsg& msg) { 
                        return pHandler->sendMsg(msg.getBuf(), msg.getBufLen(), 
                                msg.getChannelID());
                    },
                    [this, pHandler](uint32_t channelID, bool& noConn) {
                        return pHandler->canSend(channelID, noConn); 
                    },
                    &commsChannelSettings);

            // Set into the websocket handler so channel IDs match up
            pHandler->setupWebSocketChannelID(connIdx, wsChanID);

            // Debug
#ifdef DEBUG_WEBSERVER_WEBSOCKETS
            LOG_I(MODULE_PREFIX, "webSocketSetup prefix %s wsName %s protocol %s maxConn %d maxPacketSize %ld maxTxQueued %ld pingMs %ld channelID %d", 
                        interfaceName.c_str(), 
                        wsName.c_str(),
                        protocol.c_str(), 
                        maxConn, 
                        jsonConfig.getLong("pktMaxBytes", 5000),
                        jsonConfig.getLong("txQueueMax", 2),
                        jsonConfig.getLong("pingMs", 2000),
                        wsChanID);
#endif
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callback used to match endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool WebServer::restAPIMatchEndpoint(const char* url, RaftWebServerMethod method,
                    RaftWebServerRestEndpoint& endpoint)
{
    // Check valid
    if (!getRestAPIEndpointManager())
        return false;

    // Rest API match
    RestAPIEndpoint::EndpointMethod restAPIMethod = convWebToRESTAPIMethod(method);
    RestAPIEndpoint* pEndpointDef = getRestAPIEndpointManager()->getMatchingEndpoint(url, restAPIMethod, false);
    if (pEndpointDef)
    {
        endpoint.restApiFn = pEndpointDef->_callbackMain;
        endpoint.restApiFnBody = pEndpointDef->_callbackBody;
        endpoint.restApiFnChunk = pEndpointDef->_callbackChunk;
        endpoint.restApiFnIsReady = pEndpointDef->_callbackIsReady;
        return true;
    }
    return false;
}
#endif