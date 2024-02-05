/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT Manager
// Handles state of MQTT connections
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "RaftMQTTClient.h"
#include "CommsChannelMsg.h"

class RaftJsonIF;
class RestAPIEndpointManager;
class APISourceInfo;

class MQTTManager : public RaftSysMod
{
public:
    MQTTManager(const char* pModuleName, RaftJsonIF& sysConfig);

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new MQTTManager(pModuleName, sysConfig);
    }
    
protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

    // Add protocol endpoints
    virtual void addCommsChannels(CommsCoreIF& commsCoreIF) override final;

    // Get status JSON
    virtual String getStatusJSON() override final;

    // Get debug string
    virtual String getDebugJSON() override final;

private:
    // MQTT client
    RaftMQTTClient _mqttClient;

    // EndpointID used to identify this message channel to the CommsCoreIF object
    uint32_t _commsChannelID;

    // Helpers
    bool sendMQTTMsg(const String& topicName, CommsChannelMsg& msg);
    bool readyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
};
