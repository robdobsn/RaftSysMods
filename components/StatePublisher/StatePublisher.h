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
    // Constructor
    StatePublisher(const char* pModuleName, RaftJsonIF& sysConfig);
    ~StatePublisher();

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new StatePublisher(pModuleName, sysConfig);
    }
    
    // Subscription API
    RaftRetCode apiSubscription(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Receive msg generator callback function
    virtual bool registerDataSource(const char* msgGenID, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB) override final;

protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;
    
    // Add comms channels
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

    static const int32_t PUBLISHING_HANDLE_UNDEFINED = -1;
    static const uint32_t REDUCED_PUB_RATE_WHEN_BUSY_MS = 1000;
    static const uint32_t MIN_MS_BETWEEN_STATE_CHANGE_PUBLISHES = 100;

    class InterfaceRateRec
    {
    public:
        InterfaceRateRec()
        {
        }
        void setRateHz(double rateHz)
        {
            _rateHz = rateHz;
            if (_rateHz == 0)
                _betweenPubsMs = 0;
            else
                _betweenPubsMs = 1000/rateHz;
            // Fix between pubs ms as isTimout check has an average of 1ms more than requested
            if (_betweenPubsMs > 9)
                _betweenPubsMs--;
        }
        String _interface;
        String _protocol;
        double _rateHz = 1.0;
        uint32_t _betweenPubsMs = 0;
        uint32_t _lastPublishMs = 0;
        int32_t _channelID = PUBLISHING_HANDLE_UNDEFINED;

        // Indicates that the subscription for this channel/rate was created at setup
        // time (and not with a dynamic subscription API call) - persistent channels
        // are not suppressed (see below)
        bool _isPersistent = false;

        // Publishing is suppressed when the comms channel that is used for publishing
        // has indicated that it is disconnected - publishing remains suppressed until
        // the a new subscription is received
        bool _isSuppressed = false;

        // Indicates that the criteria for publishing has been met but the publishing
        // event has not occurred due to the channel being busy, etc
        bool _isPending = false;
    };

    // Publication records
    class PubRec
    {
    public:
        String _recordName;
        TriggerType_t _trigger = TRIGGER_ON_TIME_INTERVALS;
        String _msgIDStr;
        SysMod_publishMsgGenFn _msgGenFn = nullptr;
        SysMod_stateDetectCB _stateDetectFn = nullptr;;
        std::list<InterfaceRateRec> _interfaceRates;
        uint32_t _lastHashCheckMs = 0;

        // This is used by _stateDetectFn callback for state change
        // detection information - the state change is detected by
        // the comparing the returned value from the callback with
        // the previous hash value
        std::vector<uint8_t> _stateHash;
    };
    std::list<PubRec> _publicationRecs;

#ifdef DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS
    // Stats
    uint64_t _debugSlowestPublishUs = 0;
    uint64_t _debugSlowestGetHashUs = 0;
    uint32_t _debugLastShowPerfTimeMs = 0;
#endif

    // Helpers
    void cleanUp();
    CommsCoreRetCode publishData(PubRec& pubRec, InterfaceRateRec& rateRec);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "StatePub";
};
