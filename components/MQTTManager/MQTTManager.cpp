/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT Manager
// Handles state of MQTT connections
//
// Rob Dobson 2021-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string.h>
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
// #define DEBUG_MQTT_MAN_RX

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

    // Credentials (optional) and keep-alive
    String username = configGetString("username", "");
    String password = configGetString("password", "");
    uint32_t keepAliveSecs = configGetLong("keepAliveSecs", RaftMQTTClient::MQTT_DEFAULT_KEEPALIVE_TIME_SECS);

    // Setup client
    _mqttClient.setup(isMQTTEnabled, brokerHostname.c_str(), brokerPort, mqttClientID.c_str(),
                username.c_str(), password.c_str(), keepAliveSecs);

    // Messages received on inbound topics are handled as commands (like other comms channels)
    _mqttClient.setRxMsgCB(std::bind(&MQTTManager::handleRxMsg, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

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
        bool retain = topicJSON.getBool("retain", false);

        // A topic path can contain {uid} which is replaced by the system unique string - so that each
        // device can have its own topics with the same config
        topicPath.replace("{uid}", getSystemUniqueString());

        // Handle topic
        _mqttClient.addTopic(topicName.c_str(), isInbound, topicPath.c_str(), qos, retain);
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

    // Register a channel for each inbound topic - messages received are handled as commands (RICJSON) and as
    // there is nowhere to send a reply anything sent on these channels is discarded
    _mqttClient.getTopicNames(topicNames, true, false);
    for (String& topicName : topicNames)
    {
#ifdef DEBUG_MQTT_MAN_COMMS_CHANNELS
        LOG_I(MODULE_PREFIX, "addCommsChannels inbound %s", topicName.c_str());
#endif
        uint32_t channelID = commsCoreIF.registerChannel("RICJSON",
                "MQTT",
                topicName.c_str(),
                [](CommsChannelMsg& msg) { return false; },
                [](uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn) { noConn = true; return false; },
                &commsChannelSettings);
        _inboundTopicChannelIDs[topicName] = channelID;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle a message received on an inbound topic
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTManager::handleRxMsg(const String& topicName, const uint8_t* pPayload, uint32_t payloadLen)
{
    auto it = _inboundTopicChannelIDs.find(topicName);
    if ((it == _inboundTopicChannelIDs.end()) || !getCommsCore())
        return;
#ifdef DEBUG_MQTT_MAN_RX
    LOG_I(MODULE_PREFIX, "handleRxMsg topic %s channelID %d len %d", topicName.c_str(), it->second, payloadLen);
#endif
    getCommsCore()->inboundHandleMsg(it->second, pPayload, payloadLen);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String MQTTManager::getStatusJSON() const
{
    return _mqttClient.getStatusJSON();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Named values
//   connected   - 1 when connected to the broker
//   pubInFlight - QoS 1 messages published and not yet acknowledged by the broker
//   pubAcked    - QoS 1 messages acknowledged by the broker (count since startup)
//   pubLost     - QoS 1 messages published but not acknowledged before the connection closed (count since startup)
//   rxMsgs      - messages received on inbound topics (count since startup)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double MQTTManager::getNamedValue(const char* pValueName, bool& isValid)
{
    isValid = true;
    if (strcmp(pValueName, "connected") == 0)
        return _mqttClient.isConnected() ? 1 : 0;
    if (strcmp(pValueName, "pubInFlight") == 0)
        return _mqttClient.getPubInFlightCount();
    if (strcmp(pValueName, "pubAcked") == 0)
        return _mqttClient.getPubAckedCount();
    if (strcmp(pValueName, "pubLost") == 0)
        return _mqttClient.getPubLostCount();
    if (strcmp(pValueName, "rxMsgs") == 0)
        return _mqttClient.getRxMsgCount();
    isValid = false;
    return 0;
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
#ifdef DEBUG_MQTT_MAN_SEND
    String msgStr(msg.getBuf(), msg.getBufLen());
    LOG_I(MODULE_PREFIX, "sendMQTTMsg topicName %s msg %s", topicName.c_str(), msgStr.c_str());
#endif

    // Publish using client
    return _mqttClient.publishToTopic(topicName, msg.getBuf(), msg.getBufLen());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if ready to message over MQTT
// Messages wait while too many QoS 1 messages are awaiting acknowledgement and are discarded when not connected
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTManager::readyToSend(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
{
    noConn = !_mqttClient.isConnected();
    return !noConn && (_mqttClient.getPubInFlightCount() < MAX_PUB_IN_FLIGHT);
}
