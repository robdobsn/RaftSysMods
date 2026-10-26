/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT Manager
// Handles state of MQTT connections
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>
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

    // Post-setup - create subscriptions for pub sources
    virtual void postSetup() override final;

    // Status JSON
    virtual String getStatusJSON() const override final;

    // Named values (connected, pubInFlight, pubAcked, pubLost, rxMsgs)
    virtual double getNamedValue(const char* pValueName, bool& isValid) override final;

private:
    // MQTT client
    RaftMQTTClient _mqttClient;

    // EndpointID used to identify this message channel to the CommsCoreIF object
    uint32_t _commsChannelID;

    // Map of topic name to channel ID
    std::map<String, uint32_t> _topicChannelIDs;
    std::map<String, uint32_t> _inboundTopicChannelIDs;

    // Max QoS 1 messages awaiting acknowledgement before further messages wait
    static const uint32_t MAX_PUB_IN_FLIGHT = 8;

    // Helpers
    bool sendMQTTMsg(const String& topicName, CommsChannelMsg& msg);
    bool readyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);
    void handleRxMsg(const String& topicName, const uint8_t* pPayload, uint32_t payloadLen);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "MQTTMan";
};
