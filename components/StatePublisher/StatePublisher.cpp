/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// StatePublisher
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "StatePublisher.h"
#include "Logger.h"
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"
#include "RestAPIEndpointManager.h"
#include "RaftJson.h"

// Debug
// #define DEBUG_PUBLISHING_HANDLE
// #define DEBUG_PUBLISHING_MESSAGE
// #define DEBUG_PUBLISHING_REASON
// #define DEBUG_PUBLISHING_HASH
// #define DEBUG_ONLY_THIS_TOPIC "devjson"
// #define DEBUG_API_SUBSCRIPTION
// #define DEBUG_STATE_PUBLISHER_SETUP
// #define DEBUG_REDUCED_PUBLISHING_RATE_WHEN_BUSY
// #define DEBUG_FORCE_GENERATION_OF_PUBLISH_MSGS
// #define DEBUG_PUBLISH_SUPPRESS_RESTART
// #define DEBUG_NO_PUBLISH_IF_CANNOT_ACCEPT_OUTBOUND
// #define DEBUG_CONNECTION_LOST_RESTORE

// First N bytes of message to show in debug output
#define DEBUG_SHOW_ONLY_FIRST_N_BYTES_OF_MSG 16

// Show message content as text if in ASCII range
#define DEBUG_SHOW_MSG_CONTENT_AS_TEXT_IF_ASCII

// Debug
#ifdef DEBUG_ONLY_THIS_TOPIC
#include <algorithm>
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param pModuleName Module name
/// @param sysConfig System configuration
StatePublisher::StatePublisher(const char* pModuleName, RaftJsonIF& sysConfig)
        : RaftSysMod(pModuleName, sysConfig)
{
#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
    _debugLastShowPerfTimeMs = 0;
    _debugSlowestPublishUs = 0;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
StatePublisher::~StatePublisher()
{
    // Clean up
    cleanUp();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
void StatePublisher::setup()
{
    // Clear down
    cleanUp();

    // Publications info
    std::vector<String> pubList;
    if (!configGetArrayElems("pubList", pubList))
    {
        LOG_W(MODULE_PREFIX, "setup - no pubList found");
        return;
    }

    // Iterate over pubList - create publication sources only (no subscriptions)
    for (int pubIdx = 0; pubIdx < pubList.size(); pubIdx++)
    {
        // Get the publication info
        RaftJson pubInfo = pubList[pubIdx];

        // Create pub source
        PubSource pubSource;

        // Get settings (either topic or name can be used to specify topic for backwards compatibility)
        pubSource._pubTopic = pubInfo.getString("topic", pubInfo.getString("name", "").c_str());
        pubSource._msgGenFn = nullptr;
        pubSource._stateDetectFn = nullptr;

        // Add to the list of publication sources
        _pubSources.push_back(pubSource);

        // Debug
#ifdef DEBUG_STATE_PUBLISHER_SETUP
        LOG_I(MODULE_PREFIX, "setup registered publication source %s", 
                        pubSource._pubTopic.c_str());
#endif
    }

    // Debug
    LOG_I(MODULE_PREFIX, "setup num publication sources %d", (int)_pubSources.size());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop (called frequently)
void StatePublisher::loop()
{
    // Check valid
    if (!getCommsCore())
        return;

    // Check if publishing rate is to be throttled back
    bool reducePublishingRate = isSystemMainFWUpdate() || isSystemFileTransferring();

    // Iterate through active subscriptions
    for (Subscription& sub : _subscriptions)
    {

        // Calculate effective check interval based on trigger type and connection state
        uint32_t checkInterval = sub._minTimeBetweenMsgsMs;
        
        if (sub._trigger == TRIGGER_ON_TIME_INTERVALS || 
            sub._trigger == TRIGGER_ON_TIME_OR_CHANGE)
        {
            // Use the rate-based interval for time-based triggers
            if (sub._betweenPubsMs > 0)
                checkInterval = sub._betweenPubsMs;
        }
        
        // Apply backoff if connection has issues
        if (sub._connState != Subscription::CONN_STATE_ACTIVE)
        {
            checkInterval = sub._backoffIntervalMs;
        }
        else if (reducePublishingRate)
        {
            checkInterval = REDUCED_PUB_RATE_WHEN_BUSY_MS;
        }

        // Check if it's time to check/publish
        if (!Raft::isTimeout(millis(), sub._lastCheckMs, checkInterval) && !sub._isPending)
            continue;

        // Update check time
        sub._lastCheckMs = millis();

        // Check state if we have a state detect function
        bool stateChanged = false;
        std::vector<uint8_t> currentHash;
        if (sub._stateDetectFn)
        {
#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
            uint64_t startUs = micros();
#endif
            sub._stateDetectFn(sub._pubTopic.c_str(), currentHash);

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
            uint64_t getStateHashUs = micros() - startUs;
            if (_debugSlowestGetHashUs < getStateHashUs)
                _debugSlowestGetHashUs = getStateHashUs;
#endif

#ifdef DEBUG_PUBLISHING_HASH
#ifdef DEBUG_ONLY_THIS_TOPIC
            if (sub._pubTopic.equals(DEBUG_ONLY_THIS_TOPIC))
            {
#endif
                String curHashStr;
                Raft::getHexStrFromBytes(sub._lastStateHash.data(), sub._lastStateHash.size(), curHashStr);
                String newHashStr;
                Raft::getHexStrFromBytes(currentHash.data(), currentHash.size(), newHashStr);
                LOG_I(MODULE_PREFIX, "loop check hash for topic %s channelID %d curHash %s newHash %s", 
                                sub._pubTopic.c_str(), sub._channelID, curHashStr.c_str(), newHashStr.c_str());
#ifdef DEBUG_ONLY_THIS_TOPIC
            }
#endif
#endif
            
            if (currentHash != sub._lastStateHash)
            {
                stateChanged = true;
#ifdef DEBUG_FORCE_GENERATION_OF_PUBLISH_MSGS
                LOG_I(MODULE_PREFIX, "State change detected for topic %s channelID %d", 
                      sub._pubTopic.c_str(), sub._channelID);
#endif
            }
        }

        // Determine if we should publish based on trigger type
        bool shouldPublish = false;
        switch (sub._trigger)
        {
            case TRIGGER_ON_TIME_INTERVALS:
                shouldPublish = true;  // Time interval elapsed
#ifdef DEBUG_PUBLISHING_REASON
                LOG_I(MODULE_PREFIX, "loop publish due to timeout for topic %s channelID %d", 
                      sub._pubTopic.c_str(), sub._channelID);
#endif
                break;
                
            case TRIGGER_ON_STATE_CHANGE:
                shouldPublish = stateChanged;
#ifdef DEBUG_PUBLISHING_REASON
                if (shouldPublish)
                {
                    LOG_I(MODULE_PREFIX, "loop publish due to state change for topic %s channelID %d", 
                          sub._pubTopic.c_str(), sub._channelID);
                }
#endif
                break;
                
            case TRIGGER_ON_TIME_OR_CHANGE:
                shouldPublish = true;  // Either time OR change (we already checked time)
#ifdef DEBUG_PUBLISHING_REASON
                if (stateChanged)
                {
                    LOG_I(MODULE_PREFIX, "loop publish due to state change for topic %s channelID %d", 
                          sub._pubTopic.c_str(), sub._channelID);
                }
                else
                {
                    LOG_I(MODULE_PREFIX, "loop publish due to timeout for topic %s channelID %d", 
                          sub._pubTopic.c_str(), sub._channelID);
                }
#endif
                break;
                
            default:
                break;
        }

#ifdef DEBUG_PUBLISHING_REASON
        if (sub._isPending && !shouldPublish)
        {
            LOG_I(MODULE_PREFIX, "loop publish pending for topic %s channelID %d", 
                  sub._pubTopic.c_str(), sub._channelID);
        }
#endif

        // Check if pending
        if (sub._isPending)
            shouldPublish = true;

        // Attempt to publish if needed
        if (shouldPublish)
        {
            if (attemptPublish(sub))
            {
                // Successful publish - update hash if we have one
                if (sub._stateDetectFn)
                {
                    sub._lastStateHash = currentHash;
                }
            }
        }
    }

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
    if (Raft::isTimeout(millis(), _debugLastShowPerfTimeMs, 1000))
    {
        LOG_I(MODULE_PREFIX, "loop slowest publish %lld slowest stateHash %lld", 
                    _debugSlowestPublishUs, _debugSlowestGetHashUs);
        _debugSlowestPublishUs = 0;
        _debugSlowestGetHashUs = 0;
        _debugLastShowPerfTimeMs = millis();
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add REST API endpoints
/// @param endpointManager Endpoint manager
void StatePublisher::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    // Subscription to published messages
#ifdef DEBUG_API_SUBSCRIPTION
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints adding subscription endpoint");
#endif
    endpointManager.addEndpoint("subscription", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&StatePublisher::apiSubscription, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Subscription to published messages, see docs for details");
#ifdef DEBUG_API_SUBSCRIPTION
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints subscription endpoint added");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register data source (msg generator callback functions)
/// @param pubTopic Publication topic name
/// @param msgGenCB Message generator callback
/// @param stateDetectCB State detection callback
/// @return true if registration successful
bool StatePublisher::registerDataSource(const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB)
{
    // Search for publication sources using this pubTopic
    bool found = false;
    for (PubSource& pubSource : _pubSources)
    {   
        // Check topic name
        if (pubSource._pubTopic.equals(pubTopic))
        {
            LOG_I(MODULE_PREFIX, "registerDataSource registered callbacks for topic %s", pubTopic);
            pubSource._msgGenFn = msgGenCB;
            pubSource._stateDetectFn = stateDetectCB;
            found = true;
            break;
        }
    }

    // Not found?
    if (!found)
    {
        LOG_W(MODULE_PREFIX, "registerDataSource topic %s not found in config - cannot register", pubTopic);
    }
    return found;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Publish data
/// @param sub Subscription to publish to
/// @return CommsCoreRetCode
CommsCoreRetCode StatePublisher::publishData(Subscription& sub)
{
    // Check comms core
    if (!getCommsCore())
        return COMMS_CORE_RET_FAIL;

    // Endpoint message we're going to send
    CommsChannelMsg endpointMsg(sub._channelID, MSG_PROTOCOL_ROSSERIAL, 0, MSG_TYPE_PUBLISH);

    // Generate message
    bool msgOk = false;
    if (sub._msgGenFn)
    {
        msgOk = sub._msgGenFn(sub._pubTopic.c_str(), endpointMsg);
    }

#ifdef DEBUG_PUBLISHING_MESSAGE
#ifdef DEBUG_ONLY_THIS_TOPIC
    if (sub._pubTopic.equals(DEBUG_ONLY_THIS_TOPIC))
#endif
    {
        LOG_I(MODULE_PREFIX, "publishData len %d topic %s %s", endpointMsg.getBufLen(), sub._pubTopic.c_str(), msgOk ? "msgGenOk" : "msgGenFail");
    }
#endif
    if (!msgOk)
        return COMMS_CORE_RET_FAIL;
    if (endpointMsg.getBufLen() == 0)
        return COMMS_CORE_RET_FAIL;

#ifdef DEBUG_PUBLISHING_MESSAGE
#ifdef DEBUG_ONLY_THIS_TOPIC
    if (sub._pubTopic.equals(DEBUG_ONLY_THIS_TOPIC))
#endif
    {
        // Debug
        String outStr;
#ifdef DEBUG_SHOW_MSG_CONTENT_AS_TEXT_IF_ASCII
        bool isAscii = true;
        for (uint32_t i = 0; i < endpointMsg.getBufLen(); i++)
        {
            if (endpointMsg.getBuf()[i] < 1 || endpointMsg.getBuf()[i] > 126)
            {
                isAscii = false;
                break;
            }
        }
        if (isAscii)
        {
            outStr = String((char*)endpointMsg.getBuf(), endpointMsg.getBufLen());
        }
        else
#endif
        {
#ifdef DEBUG_SHOW_ONLY_FIRST_N_BYTES_OF_MSG
            Raft::getHexStrFromBytes(endpointMsg.getBuf(), 
                        endpointMsg.getBufLen() > DEBUG_SHOW_ONLY_FIRST_N_BYTES_OF_MSG ? DEBUG_SHOW_ONLY_FIRST_N_BYTES_OF_MSG : endpointMsg.getBufLen(), outStr);
            outStr += "...";
#else
            Raft::getHexStrFromBytes(endpointMsg.getBuf(), endpointMsg.getBufLen(), outStr);
#endif
        }

        LOG_I(MODULE_PREFIX, "publishData channelID %d payloadLen %d payload %s", 
                        sub._channelID, endpointMsg.getBufLen(), outStr.c_str());
    }
#endif

    // Send message
    CommsCoreRetCode retc = getCommsCore()->outboundHandleMsg(endpointMsg);

#ifdef DEBUG_PUBLISHING_MESSAGE
#ifdef DEBUG_ONLY_THIS_TOPIC
    if (sub._pubTopic.equals(DEBUG_ONLY_THIS_TOPIC))
#endif
    {
        // Debug
        LOG_I(MODULE_PREFIX, "publishData channelID %d payloadLen %d retc %d", 
                    sub._channelID, endpointMsg.getBufLen(), retc);
    }
#endif
    return retc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Subscription API
/// @param reqStr Request string
/// @param respStr Response string (output)
/// @param sourceInfo API source information
/// @return RaftRetCode
RaftRetCode StatePublisher::apiSubscription(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
#ifdef DEBUG_API_SUBSCRIPTION
    LOG_I(MODULE_PREFIX, "apiSubscription reqStr %s", reqStr.c_str());
#endif

    // Extract params
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);

    // Can't use the full request as the reqStr in the response as it won't be valid json
    String cmdName = reqStr;
    if (params.size() > 0)
        cmdName = params[0];

    // JSON params and channelID
    RaftJson jsonParams = RaftJson::getJSONFromNVPairs(nameValues, true); 
    uint32_t channelID = sourceInfo.channelID;

    // Debug
#ifdef DEBUG_API_SUBSCRIPTION
    for (RaftJson::NameValuePair& nvp : nameValues)
    {
        LOG_I(MODULE_PREFIX, "apiSubscription %s = %s", nvp.name.c_str(), nvp.value.c_str());
    }
    if (params.size() > 0)
    {
        LOG_I(MODULE_PREFIX, "apiSubscription params[0] %s", params[0].c_str());
    }
    LOG_I(MODULE_PREFIX, "subscription jsonFromNVPairs %s", jsonParams.c_str());
#endif

    // Handle subscription commands
    String actionStr = jsonParams.getString("action", "update");

#ifdef DEBUG_API_SUBSCRIPTION
    LOG_I(MODULE_PREFIX, "apiSubscription action %s", actionStr.c_str());
#endif

    // Check for record update
    if (actionStr.equalsIgnoreCase("update"))
    {
        // Get the details of the publish topics to subscribe to
        std::vector<String> pubRecsToMod;
        if (!jsonParams.getArrayElems("pubRecs", pubRecsToMod))
        {
            // That failed so try to see if a single value is present
            String pubTopic = jsonParams.getString("topic", jsonParams.getString("name", "").c_str());
            double pubRateHz = jsonParams.getDouble("rateHz", 1.0);
            uint32_t minMs = jsonParams.getInt("minMs", DEFAULT_MIN_TIME_BETWEEN_MSGS_MS);
            String pubRec = R"({"topic":")" + pubTopic + R"(","rateHz":)" + String(pubRateHz) + 
                           R"(,"minMs":)" + String(minMs) + R"(})";
            pubRecsToMod.push_back(pubRec);
        }

        // Iterate pub topics
        for (String& pubRecToMod : pubRecsToMod)
        {
            // Get details
            RaftJson pubRecConf = pubRecToMod;
            String pubTopic = pubRecConf.getString("topic", pubRecConf.getString("name", "").c_str());
            double pubRateHz = pubRecConf.getDouble("rateHz", 1.0);
            uint32_t minMs = pubRecConf.getInt("minMs", DEFAULT_MIN_TIME_BETWEEN_MSGS_MS);
            String triggerStr = pubRecConf.getString("trigger", "timeorchange");
            TriggerType_t trigger = parseTriggerType(triggerStr);

            // Verify publication source exists
            PubSource* pPubSource = findPubSource(pubTopic);
            if (!pPubSource)
            {
                LOG_W(MODULE_PREFIX, "apiSubscription unknown topic %s", pubTopic.c_str());
                continue;
            }

#ifdef DEBUG_API_SUBSCRIPTION
            LOG_I(MODULE_PREFIX, "apiSubscription topic %s rateHz %.2f minMs %d trigger %s channelID %d", 
                  pubTopic.c_str(), pubRateHz, minMs, triggerStr.c_str(), channelID);
#endif

            // Find or create subscription for this topic+channel
            Subscription* pSub = findSubscription(pubTopic, channelID);
            if (pSub)
            {
                // Update existing subscription
                pSub->setRateHz(pubRateHz);
                pSub->_minTimeBetweenMsgsMs = minMs;
                pSub->_trigger = trigger;
                pSub->_msgGenFn = pPubSource->_msgGenFn;
                pSub->_stateDetectFn = pPubSource->_stateDetectFn;
                pSub->_isPending = true;
                pSub->_connState = Subscription::CONN_STATE_ACTIVE;
                pSub->_consecutiveFailures = 0;
                pSub->_backoffIntervalMs = 0;
                
#ifdef DEBUG_API_SUBSCRIPTION
                LOG_I(MODULE_PREFIX, "apiSubscription updated subscription topic %s channelID %d rateHz %.2f minMs %d trigger %s", 
                      pubTopic.c_str(), channelID, pubRateHz, minMs, triggerStr.c_str());
#endif
            }
            else
            {
                // Create new subscription
                Subscription newSub;
                newSub._pubTopic = pubTopic;
                newSub._channelID = channelID;
                newSub.setRateHz(pubRateHz);
                newSub._minTimeBetweenMsgsMs = minMs;
                newSub._trigger = trigger;
                newSub._msgGenFn = pPubSource->_msgGenFn;
                newSub._stateDetectFn = pPubSource->_stateDetectFn;
                newSub._lastCheckMs = millis();
                newSub._isPending = true;
                newSub._connState = Subscription::CONN_STATE_ACTIVE;
                newSub._consecutiveFailures = 0;
                newSub._backoffIntervalMs = 0;
                _subscriptions.push_back(newSub);
                
#ifdef DEBUG_API_SUBSCRIPTION
                LOG_I(MODULE_PREFIX, "apiSubscription created subscription topic %s channelID %d rateHz %.2f minMs %d trigger %s", 
                      pubTopic.c_str(), channelID, pubRateHz, minMs, triggerStr.c_str());
#endif
            }
        }
    }
    else if (actionStr.equalsIgnoreCase("unsubscribe"))
    {
        // Get topic to unsubscribe from
        String pubTopic = jsonParams.getString("topic", jsonParams.getString("name", "").c_str());
        
        // Remove subscription for this channel+topic
        removeSubscription(pubTopic, channelID);
        
#ifdef DEBUG_API_SUBSCRIPTION
        LOG_I(MODULE_PREFIX, "apiSubscription unsubscribed topic %s channelID %d", pubTopic.c_str(), channelID);
#endif
    }
    
    return Raft::setJsonBoolResult(cmdName.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Clean up
void StatePublisher::cleanUp()
{
    _pubSources.clear();
    _subscriptions.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Find publication source by topic
/// @param pubTopic Topic name
/// @return Pointer to PubSource or nullptr if not found
StatePublisher::PubSource* StatePublisher::findPubSource(const String& pubTopic)
{
    for (PubSource& pubSource : _pubSources)
    {
        if (pubSource._pubTopic.equals(pubTopic))
            return &pubSource;
    }
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Find subscription by topic and channel
/// @param pubTopic Topic name
/// @param channelID Channel ID
/// @return Pointer to Subscription or nullptr if not found
StatePublisher::Subscription* StatePublisher::findSubscription(const String& pubTopic, uint32_t channelID)
{
    for (Subscription& sub : _subscriptions)
    {
        if (sub._pubTopic.equals(pubTopic) && sub._channelID == channelID)
            return &sub;
    }
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Remove subscription
/// @param pubTopic Topic name
/// @param channelID Channel ID
void StatePublisher::removeSubscription(const String& pubTopic, uint32_t channelID)
{
    for (auto it = _subscriptions.begin(); it != _subscriptions.end(); )
    {
        if (it->_pubTopic.equals(pubTopic) && it->_channelID == channelID)
        {
#ifdef DEBUG_API_SUBSCRIPTION
            LOG_I(MODULE_PREFIX, "removeSubscription topic %s channelID %d", pubTopic.c_str(), channelID);
#endif
            it = _subscriptions.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Attempt to publish
/// @param sub Subscription to publish
/// @return true if publish successful
bool StatePublisher::attemptPublish(Subscription& sub)
{
    // Mark as pending
    sub._isPending = true;

    // Check if channel can accept messages
    bool noConn = false;
    if (!getCommsCore()->outboundCanAccept(sub._channelID, MSG_TYPE_PUBLISH, noConn))
    {
#ifdef DEBUG_NO_PUBLISH_IF_CANNOT_ACCEPT_OUTBOUND
        LOG_I(MODULE_PREFIX, "attemptPublish cannot accept outbound channelID %d noConn %d", sub._channelID, noConn);
#endif
        if (noConn)
            handleConnectionLost(sub);
        return false;
    }

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
    uint64_t startUs = micros();
#endif

    // Attempt to publish
    CommsCoreRetCode retc = publishData(sub);

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
    uint64_t elapUs = micros() - startUs;
    if (_debugSlowestPublishUs < elapUs)
        _debugSlowestPublishUs = elapUs;
#endif

    if (retc == COMMS_CORE_RET_OK)
    {
        // Success!
        sub._isPending = false;
        sub._consecutiveFailures = 0;
        
        // Restore to active state if we were in backoff
        if (sub._connState != Subscription::CONN_STATE_ACTIVE)
        {
            sub._connState = Subscription::CONN_STATE_ACTIVE;
            sub._backoffIntervalMs = 0;
#ifdef DEBUG_CONNECTION_LOST_RESTORE
            LOG_I(MODULE_PREFIX, "Connection restored for topic %s channelID %d", 
                  sub._pubTopic.c_str(), sub._channelID);
#endif
        }
        return true;
    }
    else if (retc == COMMS_CORE_RET_NO_CONN)
    {
        handleConnectionLost(sub);
        return false;
    }
    else
    {
        // Other failure
        sub._consecutiveFailures++;
        if (sub._consecutiveFailures > 3)
            handleConnectionLost(sub);
        return false;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle connection lost with staged backoff
/// @param sub Subscription with connection issues
void StatePublisher::handleConnectionLost(Subscription& sub)
{
    sub._consecutiveFailures++;
    
    // State machine for staged backoff
    switch (sub._connState)
    {
        case Subscription::CONN_STATE_ACTIVE:
        case Subscription::CONN_STATE_LOST:
            if (sub._consecutiveFailures >= 3)
            {
                sub._connState = Subscription::CONN_STATE_BACKOFF_STAGE_1;
                sub._backoffIntervalMs = BACKOFF_STAGE_1_MS;
#ifdef DEBUG_CONNECTION_LOST_RESTORE
                LOG_I(MODULE_PREFIX, "Entering backoff stage 1 (%dms) for channelID %d", 
                      BACKOFF_STAGE_1_MS, sub._channelID);
#endif
            }
            else
            {
                sub._connState = Subscription::CONN_STATE_LOST;
            }
            break;
            
        case Subscription::CONN_STATE_BACKOFF_STAGE_1:
            if (sub._consecutiveFailures >= 10)
            {
                sub._connState = Subscription::CONN_STATE_BACKOFF_STAGE_2;
                sub._backoffIntervalMs = BACKOFF_STAGE_2_MS;
#ifdef DEBUG_CONNECTION_LOST_RESTORE
                LOG_I(MODULE_PREFIX, "Entering backoff stage 2 (%dms) for channelID %d", 
                      BACKOFF_STAGE_2_MS, sub._channelID);
#endif
            }
            break;
            
        case Subscription::CONN_STATE_BACKOFF_STAGE_2:
            if (sub._consecutiveFailures >= 20)
            {
                sub._connState = Subscription::CONN_STATE_BACKOFF_STAGE_3;
                sub._backoffIntervalMs = BACKOFF_STAGE_3_MS;
#ifdef DEBUG_CONNECTION_LOST_RESTORE
                LOG_I(MODULE_PREFIX, "Entering backoff stage 3 (%dms) for channelID %d", 
                      BACKOFF_STAGE_3_MS, sub._channelID);
#endif
            }
            break;
            
        case Subscription::CONN_STATE_BACKOFF_STAGE_3:
            // Stay at this stage - could optionally remove subscription after extended failure
            if (sub._consecutiveFailures >= 100)
            {
#ifdef DEBUG_CONNECTION_LOST_RESTORE
                LOG_W(MODULE_PREFIX, "Extended failure for channelID %d (100+ consecutive failures)", sub._channelID);
#endif
            }
            break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Parse trigger type from string
/// @param triggerStr Trigger type string ("time", "change", "timeorchange")
/// @return TriggerType_t
StatePublisher::TriggerType_t StatePublisher::parseTriggerType(const String& triggerStr)
{
    String lowerStr = triggerStr;
    lowerStr.toLowerCase();
    
    if (lowerStr.indexOf("change") >= 0)
    {
        if (lowerStr.indexOf("time") >= 0)
            return TRIGGER_ON_TIME_OR_CHANGE;
        else
            return TRIGGER_ON_STATE_CHANGE;
    }
    else if (lowerStr.indexOf("time") >= 0)
    {
        return TRIGGER_ON_TIME_INTERVALS;
    }
    
    // Default
    return TRIGGER_ON_TIME_INTERVALS;
}
