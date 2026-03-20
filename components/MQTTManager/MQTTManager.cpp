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
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"
#include "CommsChannelSettings.h"
#include "RestAPIEndpointManager.h"
#include "CommsCoreIF.h"
#include "SysManager.h"

// #define DEBUG_MQTT_MAN_SEND
// #define DEBUG_MQTT_MAN_COMMS_CHANNELS
// #define DEBUG_MQTT_MAN_TOPIC_SETUP
// #define DEBUG_MQTT_MAN_PUB_SOURCE_SUBS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MQTTManager::MQTTManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
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
    String mqttClientID = configGetString("clientID", getSystemUniqueString());
    if (mqttClientID == "")
        mqttClientID = getSystemName();

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
// Loop - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTManager::loop()
{
    // Service client
    _mqttClient.loop();
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

        // Store channel ID for this topic
        _topicChannelIDs[topicName] = _commsChannelID;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Post-setup - create subscriptions for pub sources defined in MQTT topic config
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTManager::postSetup()
{
    // Get the StatePublisher module
    RaftSysMod* pStatePublisher = nullptr;
    if (getSysManager())
        pStatePublisher = getSysManager()->getSysMod("Publish");
    if (!pStatePublisher)
    {
        LOG_W(MODULE_PREFIX, "postSetup StatePublisher not found");
        return;
    }

    // Read topics config and create subscriptions for any with pubSources
    std::vector<String> mqttTopics;
    configGetConfig().getArrayElems("topics", mqttTopics);
    for (uint32_t i = 0; i < mqttTopics.size(); i++)
    {
        RaftJson topicJSON = mqttTopics[i];

        // Skip inbound topics
        if (topicJSON.getBool("inbound", true))
            continue;

        // Get topic name and check we have a channel ID for it
        String topicName = topicJSON.getString("name", "");
        auto it = _topicChannelIDs.find(topicName);
        if (it == _topicChannelIDs.end())
            continue;
        uint32_t channelID = it->second;

        // Get pubSources array
        std::vector<String> pubSources;
        if (!topicJSON.getArrayElems("pubSources", pubSources))
            continue;

        // Create a subscription for each pub source
        for (const String& pubSourceJSON : pubSources)
        {
            RaftJson pubSourceConf(pubSourceJSON);
            String pubTopic = pubSourceConf.getString("pubTopic", "");
            if (pubTopic.isEmpty())
                continue;
            double rateHz = pubSourceConf.getDouble("rateHz", 1.0);
            String triggerStr = pubSourceConf.getString("trigger", "timeorchange");
            TriggerType_t trigger = TRIGGER_ON_TIME_OR_CHANGE;
            String triggerLower = triggerStr;
            triggerLower.toLowerCase();
            if (triggerLower.indexOf("change") >= 0)
            {
                if (triggerLower.indexOf("time") >= 0)
                    trigger = TRIGGER_ON_TIME_OR_CHANGE;
                else
                    trigger = TRIGGER_ON_STATE_CHANGE;
            }
            else if (triggerLower.indexOf("time") >= 0)
            {
                trigger = TRIGGER_ON_TIME_INTERVALS;
            }
            uint32_t minMs = pubSourceConf.getLong("minMs", DEFAULT_MIN_TIME_BETWEEN_MSGS_MS);

            bool ok = pStatePublisher->createSubscription(pubTopic, channelID, rateHz, trigger, minMs);
#ifdef DEBUG_MQTT_MAN_PUB_SOURCE_SUBS
            LOG_I(MODULE_PREFIX, "postSetup subscription mqttTopic %s pubSource %s channelID %d rateHz %.2f trigger %s minMs %d %s",
                  topicName.c_str(), pubTopic.c_str(), channelID, rateHz, triggerStr.c_str(), minMs, ok ? "OK" : "FAILED");
#endif
            (void)ok;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message over MQTT
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTManager::sendMQTTMsg(const String& topicName, CommsChannelMsg& msg)
{
    String msgStr(msg.getBuf(), msg.getBufLen());
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
