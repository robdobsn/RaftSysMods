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
#define DEBUG_PUBLISHING_HANDLE
#define DEBUG_PUBLISHING_MESSAGE
#define DEBUG_PUBLISHING_REASON
// #define DEBUG_PUBLISHING_HASH
// #define DEBUG_ONLY_THIS_TOPIC "ScaderOpener"
#define DEBUG_API_SUBSCRIPTION
// #define DEBUG_STATE_PUBLISHER_SETUP
// #define DEBUG_REDUCED_PUBLISHING_RATE_WHEN_BUSY
// #define DEBUG_FORCE_GENERATION_OF_PUBLISH_MSGS
// #define DEBUG_PUBLISH_SUPPRESS_RESTART
// #define DEBUG_NO_PUBLISH_IF_CANNOT_ACCEPT_OUTBOUND

// First N bytes of message to show in debug output
#define DEBUG_SHOW_ONLY_FIRST_N_BYTES_OF_MSG 16

// Show message content as text if in ASCII range
#define DEBUG_SHOW_MSG_CONTENT_AS_TEXT_IF_ASCII

// Debug
#ifdef DEBUG_ONLY_THIS_TOPIC
#include <algorithm>
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StatePublisher::StatePublisher(const char* pModuleName, RaftJsonIF& sysConfig)
        : RaftSysMod(pModuleName, sysConfig)
{
#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
    _debugLastShowPerfTimeMs = 0;
    _debugSlowestPublishUs = 0;
#endif
}

StatePublisher::~StatePublisher()
{
    // Clean up
    cleanUp();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StatePublisher::setup()
{
    // Clear down
    cleanUp();

    // Publications info
    std::vector<String> pubList;
    if (!configGetArrayElems("pubList", pubList))
    {
        LOG_I(MODULE_PREFIX, "setup - no pubList found");
        return;
    }

    // Iterate over pubList
    for (int pubIdx = 0; pubIdx < pubList.size(); pubIdx++)
    {
        // Get the publication info
        RaftJson pubInfo = pubList[pubIdx];

        // Create pubrec
        PubRec pubRec;

        // Get settings
        pubRec._pubTopic = pubInfo.getString("topic", pubInfo.getString("name", "").c_str());
        pubRec._trigger = TRIGGER_ON_TIME_OR_CHANGE;
        String triggerStr = pubInfo.getString("trigger", "timeorchange");
        triggerStr.toLowerCase();
        if (triggerStr.indexOf("change") >= 0)
        {
            pubRec._trigger = TRIGGER_ON_STATE_CHANGE;
            if (triggerStr.indexOf("time") >= 0)
                pubRec._trigger = TRIGGER_ON_TIME_OR_CHANGE;
        }
        if (pubRec._trigger == TRIGGER_NONE)
        {
            pubRec._trigger = TRIGGER_ON_TIME_INTERVALS;
        }
        pubRec._minStateChangeMs = pubInfo.getInt("minStateChangeMs", MIN_MS_BETWEEN_STATE_CHANGE_PUBLISHES);

        RaftJson interfacesJson = pubInfo.getString("ifs", pubInfo.getString("rates", "").c_str());

        // Check for interfaces
        int numInterfaces = 0;
        if (interfacesJson.getType("", numInterfaces) == RaftJson::RAFT_JSON_ARRAY)
        {
            // Iterate interfaces
            for (int rateIdx = 0; rateIdx < numInterfaces; rateIdx++)
            {
                // Get the interface info
                RaftJson interfaceInfo = interfacesJson.getString(("["+String(rateIdx)+"]").c_str(), "{}");
                String interface = interfaceInfo.getString("if", "");
                String protocol = interfaceInfo.getString("protocol", "");
                double rateHz = interfaceInfo.getDouble("rateHz", 1.0);

                // Add to list
                PubInterfaceRec ifRec;
                ifRec._interface = interface;
                ifRec._protocol = protocol;
                ifRec.setRateHz(rateHz);
                ifRec._lastPublishMs = millis();
                ifRec._isPersistent = true;
                ifRec._isSuppressed = false;
                pubRec._interfaceRecs.push_back(ifRec);

                // Debug
#ifdef DEBUG_STATE_PUBLISHER_SETUP
                LOG_I(MODULE_PREFIX, "setup publishIF %s rateHz %.1f msBetween %d topic %s protocol %s", interface.c_str(),
                                rateHz, ifRec._betweenPubsMs, pubRec._pubTopic.c_str(), ifRec._protocol.c_str());
#endif
            }

            // Add to the list of publication records
            _publicationRecs.push_back(pubRec);
        }
    }

    // Debug
    LOG_I(MODULE_PREFIX, "setup num publication recs %d", _publicationRecs.size());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop (called frequently)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StatePublisher::loop()
{
    // Check valid
    if (!getCommsCore())
        return;

    // Check if publishing rate is to be throttled back
    bool reducePublishingRate = isSystemMainFWUpdate() || isSystemFileTransferring();

    // Check through publishers
    for (PubRec& pubRec : _publicationRecs)
    {
#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
        uint64_t getStateHashUs = 0;
#endif

        // Check for state change detection callback
        bool publishDueToStateChange = false;
        if (pubRec._stateDetectFn && ((pubRec._trigger == TRIGGER_ON_STATE_CHANGE) || (pubRec._trigger == TRIGGER_ON_TIME_OR_CHANGE)))
        {
            // Check for the mimimum time between publications
#ifdef DEBUG_PUBLISHING_HASH
            if (Raft::isTimeout(millis(), pubRec._lastHashCheckMs, MIN_MS_BETWEEN_STATE_CHANGE_PUBLISHES*10))
#else
            if (Raft::isTimeout(millis(), pubRec._lastHashCheckMs, pubRec._minStateChangeMs))
#endif
            {
                // Last hash check time
                pubRec._lastHashCheckMs = millis();

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
                uint64_t startUs = micros();
#endif

                // Callback function generates a hash in the form of a std::vector<uint8_t>
                // If this is not identical to previously returned hash then force message generation
                std::vector<uint8_t> newStateHash;
                pubRec._stateDetectFn(pubRec._pubTopic.c_str(), newStateHash);

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
                getStateHashUs = micros() - startUs;
                if (_debugSlowestGetHashUs < getStateHashUs)
                    _debugSlowestGetHashUs = getStateHashUs;
#endif

#ifdef DEBUG_PUBLISHING_HASH
#ifdef DEBUG_ONLY_THIS_TOPIC
                if (pubRec._pubTopic.equals(DEBUG_ONLY_THIS_TOPIC))
                {
#endif
                    String curHashStr;
                    Raft::getHexStrFromBytes(pubRec._stateHash.data(), pubRec._stateHash.size(), curHashStr);
                    String newHashStr;
                    Raft::getHexStrFromBytes(newStateHash.data(), newStateHash.size(), newHashStr);
                    LOG_I(MODULE_PREFIX, "loop check hash for topic %s curHash %s newHash %s", 
                                    pubRec._pubTopic.c_str(), curHashStr.c_str(), newHashStr.c_str());
#ifdef DEBUG_ONLY_THIS_TOPIC
                }
#endif
#endif

                // Check hash value
                if(pubRec._stateHash != newStateHash)
                {
                    publishDueToStateChange = true;
                    pubRec._stateHash = newStateHash;
#ifdef DEBUG_FORCE_GENERATION_OF_PUBLISH_MSGS
                    LOG_I(MODULE_PREFIX, "Force generation on state change for topic %s", pubRec._pubTopic.c_str());
#endif
                }
            }
        }

        // And each interface
        for (PubInterfaceRec& rateRec : pubRec._interfaceRecs)
        {
            // Check if interface is suppressed
            if (rateRec._isSuppressed || !((pubRec._trigger == TRIGGER_ON_TIME_OR_CHANGE )|| (pubRec._trigger == TRIGGER_ON_TIME_INTERVALS)))
                continue;

            // Check for time to publish
            bool publishTime = (rateRec._rateHz != 0) && Raft::isTimeout(millis(), rateRec._lastPublishMs, 
                                    reducePublishingRate ? REDUCED_PUB_RATE_WHEN_BUSY_MS : rateRec._betweenPubsMs);

#ifdef DEBUG_PUBLISHING_REASON
            const char* ifStr = rateRec._interface.length() == 0 ? "<ALL>" : rateRec._interface.c_str();
            if (publishDueToStateChange)
            {
                LOG_I(MODULE_PREFIX, "loop publish due to state change for topic %s i/f %s", pubRec._pubTopic.c_str(), ifStr);
            }
            else if (publishTime)
            {
                LOG_I(MODULE_PREFIX, "loop publish due to timeout for topic %s i/f %s", pubRec._pubTopic.c_str(), ifStr);
            }
            else if (rateRec._isPending)
            {
                LOG_I(MODULE_PREFIX, "loop publish pending for topic %s i/f %s", pubRec._pubTopic.c_str(), ifStr);
            }
#endif
            // Check for publish required
            if (publishDueToStateChange || publishTime || rateRec._isPending)
            {
                // Publish is pending
                rateRec._isPending = true;

                // Check if channelID is defined
                if (rateRec._channelID == PUBLISHING_HANDLE_UNDEFINED)
                {
                    // Get a match of interface and protocol
                    rateRec._channelID = getCommsCore()->getChannelIDByName(rateRec._interface, rateRec._protocol);

#ifdef DEBUG_PUBLISHING_HANDLE
                    // Debug
                    LOG_I(MODULE_PREFIX, "Got channelID %d for topic %s i/f %s protocol %s", rateRec._channelID, pubRec._pubTopic.c_str(),
                            rateRec._interface.length() == 0 ? "<ALL>" : rateRec._interface.c_str(), 
                            rateRec._protocol.c_str());
#endif

                    // Still undefined?
                    if (rateRec._channelID == PUBLISHING_HANDLE_UNDEFINED)
                        continue;
                }

                // Check if interface can accept messages
                bool noConn = false;
                if (getCommsCore()->outboundCanAccept(rateRec._channelID, MSG_TYPE_PUBLISH, noConn))
                {

#ifdef DEBUG_REDUCED_PUBLISHING_RATE_WHEN_BUSY
                    if (reducePublishingRate)
                    {
                        LOG_I(MODULE_PREFIX, "loop publishing rate reduced for channel %d", rateRec._channelID);
                    }
#endif

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
                    uint64_t startUs = micros();
#endif

                    CommsCoreRetCode publishRetc = publishData(pubRec, rateRec);

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
                    uint64_t elapUs = micros() - startUs;
                    if (_debugSlowestPublishUs < elapUs)
                        _debugSlowestPublishUs = elapUs;
                    if (Raft::isTimeout(millis(), _debugLastShowPerfTimeMs, 1000))
                    {
                        LOG_I(MODULE_PREFIX, "loop slowest publish %lld slowest stateHash %lld", 
                                    _debugSlowestPublishUs, _debugSlowestGetHashUs);
                        _debugSlowestPublishUs = 0;
                        _debugSlowestGetHashUs = 0;
                        _debugLastShowPerfTimeMs = millis();
                    }
#endif

                    // Check for no connection
                    if (publishRetc == COMMS_CORE_RET_NO_CONN)
                    {
                        noConn = true;
                    }
                    // Publish no longer pending (whether successful or not)
                    rateRec._isPending = false;
                    rateRec._lastPublishMs = millis();
                }
                else
                {
#ifdef DEBUG_NO_PUBLISH_IF_CANNOT_ACCEPT_OUTBOUND
                    LOG_I(MODULE_PREFIX, "loop cannot accept outbound for channel %d noConn %d", rateRec._channelID, noConn);
#endif
                }

                // Check if there is no connection on this channel - if so then check if the rateRec is
                // persistent and, if not, then suppress publishing this rateRec
                if (noConn && !rateRec._isPersistent)
                {
#ifdef DEBUG_PUBLISH_SUPPRESS_RESTART
                    LOG_I(MODULE_PREFIX, "loop suppressing rateRec channelID %d", rateRec._channelID);
#endif
                    rateRec._isSuppressed = true;
                }
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Register data source (msg generator callback functions)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool StatePublisher::registerDataSource(const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB)
{
    // Search for publication records using this pubTopic
    bool found = false;
    for (PubRec& pubRec : _publicationRecs)
    {   
        // Check ID
        if (pubRec._pubTopic.equals(pubTopic))
        {
            LOG_I(MODULE_PREFIX, "registerDataSource registered msgGenFn for topic %s", pubTopic);
            pubRec._msgGenFn = msgGenCB;
            pubRec._stateDetectFn = stateDetectCB;
            found = true;
            break;
        }
    }

    // Not found?
    if (!found)
    {
        LOG_W(MODULE_PREFIX, "registerDataSource msgGenFn not registered for topic %s", pubTopic);
    }
    return found;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Publish data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommsCoreRetCode StatePublisher::publishData(StatePublisher::PubRec& pubRec, PubInterfaceRec& rateRec)
{
    // Check comms core
    if (!getCommsCore())
        return COMMS_CORE_RET_FAIL;

    // Endpoint message we're going to send
    CommsChannelMsg endpointMsg(rateRec._channelID, MSG_PROTOCOL_ROSSERIAL, 0, MSG_TYPE_PUBLISH);

    // Generate message
    bool msgOk = false;
    if (pubRec._msgGenFn)
    {
        msgOk = pubRec._msgGenFn(pubRec._pubTopic.c_str(), endpointMsg);
    }

#ifdef DEBUG_PUBLISHING_MESSAGE
#ifdef DEBUG_ONLY_THIS_TOPIC
    if (pubRec._pubTopic.equals(DEBUG_ONLY_THIS_TOPIC))
#endif
    {
        LOG_I(MODULE_PREFIX, "publishData len %d topic %s %s", endpointMsg.getBufLen(), pubRec._pubTopic.c_str(), msgOk ? "msgGenOk" : "msgGenFail");
    }
#endif
    if (!msgOk)
        return COMMS_CORE_RET_FAIL;
    if (endpointMsg.getBufLen() == 0)
        return COMMS_CORE_RET_FAIL;

#ifdef DEBUG_PUBLISHING_MESSAGE
#ifdef DEBUG_ONLY_THIS_TOPIC
    if (pubRec._pubTopic.equals(DEBUG_ONLY_THIS_TOPIC))
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

        LOG_I(MODULE_PREFIX, "publishData if %s channelID %d payloadLen %d payload %s", 
                        rateRec._interface.length() == 0 ? "<ALL>" : rateRec._interface.c_str(), 
                        rateRec._channelID, endpointMsg.getBufLen(), outStr.c_str());
    }
#endif

    // Send message
    CommsCoreRetCode retc = getCommsCore()->outboundHandleMsg(endpointMsg);

#ifdef DEBUG_PUBLISHING_MESSAGE
#ifdef DEBUG_ONLY_THIS_TOPIC
    if (pubRec._pubTopic.equals(DEBUG_ONLY_THIS_TOPIC))
#endif
    {
        // Debug
        LOG_I(MODULE_PREFIX, "publishData channelID %d payloadLen %d retc %d", 
                    rateRec._channelID, endpointMsg.getBufLen(), retc);
    }
#endif
    return retc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Subscription
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
    String actionStr = jsonParams.getString("action", "");

#ifdef DEBUG_API_SUBSCRIPTION
    LOG_I(MODULE_PREFIX, "apiSubscription %s", actionStr.c_str());
#endif

    // Check for record update
    if (actionStr.equalsIgnoreCase("update"))
    {
        // Get the details of the publish records to alter - initially try to get
        // these from array of values
        std::vector<String> pubRecsToMod;
        if (!jsonParams.getArrayElems("pubRecs", pubRecsToMod))
        {
            // That failed so try to see if a single value is present and create an array with
            // a single element if so
            String pubTopic = jsonParams.getString("topic", jsonParams.getString("name", "").c_str());
            double pubRateHz = jsonParams.getDouble("rateHz", 1.0);
            String pubRec = R"({"topic":")" + pubTopic + R"(","rateHz":)" + String(pubRateHz) + R"(})";
            pubRecsToMod.push_back(pubRec);
        }

        // Iterate pub record names
        for (String& pubRecToMod : pubRecsToMod)
        {
            // Get details of changes
            RaftJson pubRecConf = pubRecToMod;
            String pubTopic = pubRecConf.getString("topic", pubRecConf.getString("name", "").c_str());
            double pubRateHz = pubRecConf.getDouble("rateHz", 1.0);

            // Find the existing publication record to update
#ifdef DEBUG_API_SUBSCRIPTION
            LOG_I(MODULE_PREFIX, "apiSubscription pubRec info %s", pubRecToMod.c_str());
#endif
            for (PubRec& pubRec : _publicationRecs)
            {
                // Check name
                if (!pubRec._pubTopic.equals(pubTopic))
                    continue;

                // Update interface-rate record (if there is one)
                bool interfaceRecFound = false;
                for (PubInterfaceRec& rateRec : pubRec._interfaceRecs)
                {
                    if (rateRec._channelID == channelID)
                    {
                        interfaceRecFound = true;
                        rateRec.setRateHz(pubRateHz);
                        rateRec._isPending = true;
                        rateRec._lastPublishMs = millis();
                        rateRec._interface = "Subscr_ch_" + String(channelID);
#ifdef DEBUG_PUBLISH_SUPPRESS_RESTART
                        if (rateRec._isSuppressed)
                        {
                            LOG_I(MODULE_PREFIX, "apiSubscription restoring suppressed rateRec channelID %d", channelID);
                        }   
#endif                     
                        rateRec._isSuppressed = false;
#ifdef DEBUG_API_SUBSCRIPTION
                        LOG_I(MODULE_PREFIX, "apiSubscription updated rateRec channelID %d rateHz %.2f", channelID, pubRateHz);
#endif
                    }
                }

                // Check we found an existing record
                if (!interfaceRecFound)
                {
                    // No record found so create one
                    PubInterfaceRec ifRec;
                    ifRec._channelID = channelID;
                    ifRec.setRateHz(pubRateHz);
                    ifRec._lastPublishMs = millis();
                    ifRec._isPending = true;
                    ifRec._isSuppressed = false;
                    pubRec._interfaceRecs.push_back(ifRec);
#ifdef DEBUG_API_SUBSCRIPTION
                    LOG_I(MODULE_PREFIX, "apiSubscription created rateRec channelID %d rateHz %.2f", channelID, pubRateHz);
#endif
                }
                break;
            }
        }
    }
    return Raft::setJsonBoolResult(cmdName.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StatePublisher::cleanUp()
{
}
