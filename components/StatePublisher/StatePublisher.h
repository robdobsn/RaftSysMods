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

class RobotController;

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

    /// @brief Register data source (msg generator callback function)
    /// @param pubTopic Publication topic name
    /// @param msgGenCB Message generator callback
    /// @param stateDetectCB State detection callback
    /// @return true if registration successful
    virtual bool registerDataSource(const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB) override final;

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

private:
    enum TriggerType_t
    {
        TRIGGER_NONE,
        TRIGGER_ON_TIME_INTERVALS,
        TRIGGER_ON_STATE_CHANGE,
        TRIGGER_ON_TIME_OR_CHANGE
    };

    static const uint32_t REDUCED_PUB_RATE_WHEN_BUSY_MS = 1000;
    static const uint32_t DEFAULT_MIN_TIME_BETWEEN_MSGS_MS = 100;
    static const uint32_t BACKOFF_STAGE_1_MS = 5000;
    static const uint32_t BACKOFF_STAGE_2_MS = 30000;
    static const uint32_t BACKOFF_STAGE_3_MS = 60000;

    // Publication source - defines what can be published and specifies the callbacks
    // used to get the data and detect state changes via a hash
    class PubSource
    {
    public:
        String _pubTopic;
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
        uint32_t _backoffIntervalMs = 0;            // Current backoff interval
        uint32_t _consecutiveFailures = 0;          // Count failures for backoff logic
    };

    // Publication sources (from config, never changes after setup and registerDataSource)
    std::list<PubSource> _pubSources;

    // Active subscriptions (created/modified via API)
    std::list<Subscription> _subscriptions;

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
    // Stats
    uint64_t _debugSlowestPublishUs = 0;
    uint64_t _debugSlowestGetHashUs = 0;
    uint32_t _debugLastShowPerfTimeMs = 0;
#endif

    // Helpers
    void cleanUp();
    PubSource* findPubSource(const String& pubTopic);
    Subscription* findSubscription(const String& pubTopic, uint32_t channelID);
    void removeSubscription(const String& pubTopic, uint32_t channelID);
    bool attemptPublish(Subscription& sub);
    void handleConnectionLost(Subscription& sub);
    CommsCoreRetCode publishData(Subscription& sub);
    static TriggerType_t parseTriggerType(const String& triggerStr);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "StatePub";
};
