/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT Manager
// Handles state of MQTT connections
//
// Rob Dobson 2021-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "MQTTManager.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "ESPUtils.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"
#include "CommsChannelSettings.h"
#include "RestAPIEndpointManager.h"
#include "CommsCoreIF.h"
#include "SysManager.h"

// #define DEBUG_MQTT_MAN_SEND
// #define DEBUG_MQTT_MAN_COMMS_CHANNELS
// #define DEBUG_MQTT_MAN_TOPIC_SETUP

// Log prefix
#if defined(DEBUG_MQTT_MAN_SEND) || defined(DEBUG_MQTT_MAN_COMMS_CHANNELS) || defined(DEBUG_MQTT_MAN_TOPIC_SETUP)
static const char *MODULE_PREFIX = "MQTTMan";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MQTTManager::MQTTManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : SysModBase(pModuleName, sysConfig)
{
    // ChannelID
    _commsChannelID = CommsCoreIF::CHANNEL_ID_UNDEFINED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTManager::setup()
{
    // Extract info from config
    bool isMQTTEnabled = configGetBool("enable", false);
    String brokerHostname = configGetString("brokerHostname", "");
    uint32_t brokerPort = configGetLong("brokerPort", RaftMQTTClient::DEFAULT_MQTT_PORT);
    String mqttClientID = configGetString("clientID", "");

    // Form unique client ID
    mqttClientID += getSystemUniqueString();

    // Setup client
    _mqttClient.setup(isMQTTEnabled, brokerHostname.c_str(), brokerPort, mqttClientID.c_str());

    // Handle topics
    std::vector<String> mqttTopics;
    configGetConfig().getArrayElems("topics", mqttTopics);
#ifdef DEBUG_MQTT_MAN_TOPIC_SETUP
    LOG_I(MODULE_PREFIX, "setup topics %d", mqttTopics.size());
#endif
    for (uint32_t i = 0; i < mqttTopics.size(); i++)
    {
        // Extract topic details
        RaftJson topicJSON = mqttTopics[i];

        // Check direction
        String defaultName = "topic" + String(i+1);
        String topicName = topicJSON.getString("name", defaultName.c_str());
        bool isInbound = topicJSON.getBool("inbound", true);
        String topicPath = topicJSON.getString("path", "");
        uint8_t qos = topicJSON.getLong("qos", 0);

        // Handle topic
        _mqttClient.addTopic(topicName.c_str(), isInbound, topicPath.c_str(), qos);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTManager::service()
{
    // Service client
    _mqttClient.service();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get status JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String MQTTManager::getStatusJSON()
{
    return "{}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String MQTTManager::getDebugJSON()
{
    return "{}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTManager::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
}

void MQTTManager::addCommsChannels(CommsCoreIF& commsCoreIF)
{
    // Get a list of outbound topic names
    std::vector<String> topicNames;
    _mqttClient.getTopicNames(topicNames, false, true);
#ifdef DEBUG_MQTT_MAN_COMMS_CHANNELS
    LOG_I(MODULE_PREFIX, "addCommsChannels numOutTopics %d", topicNames.size());
#endif

    // Comms channel
    static const CommsChannelSettings commsChannelSettings;

    // Register an endpoint for each
    for (String& topicName : topicNames)
    {
#ifdef DEBUG_MQTT_MAN_COMMS_CHANNELS        
        LOG_I(MODULE_PREFIX, "addCommsChannels %s", topicName.c_str());
#endif
        // Register as a channel
        _commsChannelID = commsCoreIF.registerChannel("RICJSON", 
                "MQTT",
                topicName.c_str(),
                [this, topicName](CommsChannelMsg& msg) { return sendMQTTMsg(topicName, msg); },
                std::bind(&MQTTManager::readyToSend, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                &commsChannelSettings);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message over MQTT
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTManager::sendMQTTMsg(const String& topicName, CommsChannelMsg& msg)
{
    String msgStr;
    Raft::strFromBuffer(msg.getBuf(), msg.getBufLen(), msgStr);
#ifdef DEBUG_MQTT_MAN_SEND
    LOG_I(MODULE_PREFIX, "sendMQTTMsg topicName %s msg %s", topicName.c_str(), msgStr.c_str());
#endif

    // Publish using client
    return _mqttClient.publishToTopic(topicName, msgStr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if ready to message over MQTT
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTManager::readyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
{
    return true;
}
