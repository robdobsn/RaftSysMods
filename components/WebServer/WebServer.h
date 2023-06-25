/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WebServer
//
// Rob Dobson 2012-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef FEATURE_WEB_SERVER_OR_WEB_SOCKETS

#include <ConfigBase.h>
#include <RestAPIEndpointManager.h>
#include <SysModBase.h>
#include <RaftWebInterface.h>
#include <CommsChannelSettings.h>

class WebServerResource;
class CommsChannelMsg;

#include "RaftWebServer.h"

class WebServer : public SysModBase
{
public:
    // Constructor/destructor
    WebServer(const char* pModuleName, ConfigBase& defaultConfig, 
            ConfigBase* pGlobalConfig, ConfigBase* pMutableConfig);
    virtual ~WebServer();

    // Begin
    void beginServer();

    // Add resources: servable content defined in code
    // @param pResources array of resource definitions
    // @param numResources number of resource definitions
    void addStaticResources(const WebServerResource *pResources, int numResources);

    // Serve static files from the file system
    // @param servePaths (comma separated and can include uri=path pairs separated by =)
    // @param cacheControl (nullptr or cache control header value eg "no-cache, no-store, must-revalidate")
    void serveStaticFiles(const char* servePaths, const char* cacheControl = NULL);
    
    // Server-side event handler (one-way text to browser)
    void enableServerSideEvents(const String& eventsURL);
    void sendServerSideEvent(const char* eventContent, const char* eventGroup);

protected:
    // Setup
    virtual void setup() override final;

    // Service - called frequently
    virtual void service() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;

    // Add comms channels
    virtual void addCommsChannels(CommsCoreIF& commsCoreIF) override final
    {
    }
    
private:
    // Helpers
    void addStaticResource(const WebServerResource *pResource, const char *pAliasPath = nullptr);
    void configChanged();
    void applySetup();
    void setupEndpoints();
    bool restAPIMatchEndpoint(const char* url, RaftWebServerMethod method,
                    RaftWebServerRestEndpoint& endpoint);
    void webSocketSetup();

    // Server config
    bool _webServerEnabled = false;
    uint32_t _port = RaftWebServerSettings::DEFAULT_HTTP_PORT;
    String _restAPIPrefix = RaftWebServerSettings::DEFAULT_REST_API_PREFIX;

    // Web server setup
    bool _isWebServerSetup = false;

    // Server
    RaftWebServer _raftWebServer;

    // Singleton
    static WebServer* _pThisWebServer;

    // Websockets
    std::vector<String> _webSocketConfigs;

    // Mapping from web-server method to RESTAPI method enums
    RestAPIEndpoint::EndpointMethod convWebToRESTAPIMethod(RaftWebServerMethod method)
    {
        switch(method)
        {
            case WEB_METHOD_POST: return RestAPIEndpoint::ENDPOINT_POST;
            case WEB_METHOD_PUT: return RestAPIEndpoint::ENDPOINT_PUT;
            case WEB_METHOD_DELETE: return RestAPIEndpoint::ENDPOINT_DELETE;
            case WEB_METHOD_OPTIONS: return RestAPIEndpoint::ENDPOINT_OPTIONS;
            default: return RestAPIEndpoint::ENDPOINT_GET;
        }
    }
};

#endif
