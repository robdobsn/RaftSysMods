/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// StatePublisher
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <vector>
#include "RaftArduino.h"
#include "APISourceInfo.h"
#include "RaftSysMod.h"
#include "CommsCoreIF.h"

// Enable connection backoff mechanism (staged retry delays when connection fails)
// Uncomment following line to enable backoff
#define ENABLE_CONNECTION_BACKOFF

// Show performance stats in debug every N seconds
// #define DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS

class StatePublisher : public RaftSysMod
{
public:
    /// @brief Constructor
    /// @param pModuleName Module name
    /// @param sysConfig System configuration
    StatePublisher(const char* pModuleName, RaftJsonIF& sysConfig);
    ~StatePublisher();

    /// @brief Create function (for use by SysManager factory)
    /// @param pModuleName Module name
    /// @param sysConfig System configuration
    /// @return Pointer to new StatePublisher instance
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new StatePublisher(pModuleName, sysConfig);
    }
    
    /// @brief Subscription API
    /// @param reqStr Request string
    /// @param respStr Response string (output)
    /// @param sourceInfo API source information
    /// @return RaftRetCode
    RaftRetCode apiSubscription(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    /// @brief Get publish topics API
    /// @param reqStr Request string
    /// @param respStr Response string (output)
    /// @param sourceInfo API source information
    /// @return RaftRetCode
    RaftRetCode apiGetPubTopics(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    /// @brief Register data source (msg generator callback function)
    /// @param pubTopic Publication topic name
    /// @param msgGenCB Message generator callback
    /// @param stateDetectCB State detection callback
    /// @return Topic index (0-based) or UINT16_MAX on failure
    virtual uint16_t registerDataSource(const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB) override final;

    /// @brief Create a subscription programmatically
    /// @param pubTopic Publication topic name (must match a registered data source)
    /// @param channelID Comms channel ID to publish to
    /// @param rateHz Publishing rate in Hz
    /// @param trigger type for the subscription, or TRIGGER_NONE on failure
    /// @param minTimeBetweenMsgsMs Minimum time between messages in milliseconds (to prevent flooding when trigger is state change)
    /// @return true if subscription created/updated successfully
    virtual bool createSubscription(const String& pubTopic, uint32_t channelID, double rateHz, 
            TriggerType_t trigger = TRIGGER_ON_TIME_OR_CHANGE, 
            uint32_t minTimeBetweenMsgsMs = DEFAULT_MIN_TIME_BETWEEN_MSGS_MS) override final;

protected:
    /// @brief Setup
    virtual void setup() override final;

    /// @brief Loop - called frequently
    virtual void loop() override final;

    /// @brief Add REST API endpoints
    /// @param endpointManager Endpoint manager
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;
    
    /// @brief Add comms channels
    /// @param commsCore Communications core
    virtual void addCommsChannels(CommsCoreIF& commsCore) override final
    {
    }

    /// @brief Parse trigger type from string
    /// @param triggerStr 
    /// @return TriggerType_t, or TRIGGER_NONE if unknown
    static TriggerType_t parseTriggerType(const String& triggerStr);

private:
    // Reduce publish rate to 10% when busy
    static const uint32_t PUB_RATE_PERCENT_WHEN_BUSY = 10;

#ifdef ENABLE_CONNECTION_BACKOFF
    // Backoff stages percentages
    static const uint32_t BACKOFF_STAGE_1_PERCENT = 50;
    static const uint32_t BACKOFF_STAGE_2_PERCENT = 90;
    static const uint32_t BACKOFF_STAGE_3_PERCENT = 95;
#endif

    // Publication source - defines what can be published and specifies the callbacks
    // used to get the data and detect state changes via a hash
    class PubSource
    {
    public:
        String _pubTopic;
        uint16_t _topicIndex = UINT16_MAX;
        SysMod_publishMsgGenFn _msgGenFn = nullptr;
        SysMod_stateDetectCB _stateDetectFn = nullptr;
    };

    // Active subscription - created on-demand via API
    class Subscription
    {
    public:
        Subscription()
        {
        }
        void setRateHz(double rateHz)
        {
            _rateHz = rateHz;
            if (_rateHz == 0)
                _betweenPubsMs = 0;
            else
                _betweenPubsMs = 1000/rateHz;
            // Fix between pubs ms as isTimeout check has an average of 1ms more than requested
            if (_betweenPubsMs > 9)
                _betweenPubsMs--;
        }

        String _pubTopic;                           // Which topic to publish
        uint16_t _topicIndex = UINT16_MAX;          // Topic index (0-based)
        uint32_t _channelID = 0;                    // Which channel to publish to
        TriggerType_t _trigger = TRIGGER_ON_TIME_OR_CHANGE; // When to publish
        double _rateHz = 1.0;                       // Publishing rate
        uint32_t _betweenPubsMs = 0;                // Calculated from rateHz
        uint32_t _minTimeBetweenMsgsMs = DEFAULT_MIN_TIME_BETWEEN_MSGS_MS; // Minimum interval between messages
        uint32_t _lastCheckMs = 0;                  // Last time we checked/attempted publish
        bool _isPending = false;                    // Publish is due but not sent yet

        // Callbacks (copied from PubSource for performance - avoids lookup in loop)
        SysMod_publishMsgGenFn _msgGenFn = nullptr;
        SysMod_stateDetectCB _stateDetectFn = nullptr;

        // State tracking - each subscription tracks what it has seen/published
        std::vector<uint8_t> _lastStateHash;

#ifdef ENABLE_CONNECTION_BACKOFF
        // Connection state management
        enum ConnState
        {
            CONN_STATE_ACTIVE,              // Publishing normally
            CONN_STATE_LOST,                // Connection lost, trying fast retry
            CONN_STATE_BACKOFF_STAGE_1,     // Slower retry (5s)
            CONN_STATE_BACKOFF_STAGE_2,     // Even slower (30s)
            CONN_STATE_BACKOFF_STAGE_3      // Slowest retry (60s)
        };
        ConnState _connState = CONN_STATE_ACTIVE;
        uint32_t _backoffPercent = 0;            // Current backoff percentage
        uint32_t _consecutiveFailures = 0;          // Count failures for backoff logic

        // Calculation intervalMs with backoff applied
        uint32_t calculateIntervalIncreaseMs(uint32_t reductionToPercent, uint32_t curIntervalMs)
        {
            if (reductionToPercent <= 0)
                return curIntervalMs;
            if (reductionToPercent >= 100)
                return curIntervalMs * 10; // Max 10x delay
            return curIntervalMs * 100 / (100 - reductionToPercent);
        }
#endif
    };

    // Publication sources (from config, never changes after setup and registerDataSource)
    std::list<PubSource> _pubSources;

    // Active subscriptions (created/modified via API)
    std::list<Subscription> _subscriptions;

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
    // Stats
    uint64_t _debugSlowestPublishUs = 0;
    uint64_t _debugSlowestGetHashUs = 0;
    uint64_t _debugSlowestLoopUs = 0;
    uint32_t _debugPublishCount = 0;
    uint32_t _debugLastShowPerfTimeMs = 0;
#endif

    // Helpers
    void cleanUp();
    PubSource* findPubSource(const String& pubTopic);
    Subscription* findSubscription(const String& pubTopic, uint32_t channelID);
    void removeSubscription(const String& pubTopic, uint32_t channelID);
    bool attemptPublish(Subscription& sub);
#ifdef ENABLE_CONNECTION_BACKOFF
    void handleConnectionLost(Subscription& sub);
#endif
    CommsCoreRetCode publishData(Subscription& sub);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "StatePub";
};
