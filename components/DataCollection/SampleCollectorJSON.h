/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SampleCollector
// Collects samples in memory
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "RaftSysMod.h"
#include "SpiramAwareAllocator.h"
#include "APISourceInfo.h"
#include "RestAPIEndpointManager.h"
#include "RaftUtils.h"
#include "FileSystem.h"

class SampleCollectorJSON : public RaftSysMod
{
public:
    SampleCollectorJSON(const char *pModuleName, RaftJsonIF& sysConfig)
        :   RaftSysMod(pModuleName, sysConfig)
    {
    }
    virtual ~SampleCollectorJSON()
    {
    }

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new SampleCollectorJSON(pModuleName, sysConfig);
    }    

    // Add sample
    bool addSample(const String& sampleJSON);

protected:

    // Setup
    virtual void setup() override final;

    // Loop
    virtual void loop() override final
    {}

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

    // Receive JSON command
    virtual RaftRetCode receiveCmdJSON(const char* cmdJSON) override final
    {
        addSample(cmdJSON);
        return RAFT_OK;
    }

private:
    // Sample API name
    String _sampleAPIName;

    // Header string
    String _sampleHeader;

    // Sample info
    uint32_t _sampleRateLimitHz = 0;
    uint32_t _maxTotalJSONStringSize = 0;

    // Dumping
    bool _dumpToConsoleWhenFull = false;
    String _dumpToFileName;
    uint32_t _maxFileSize = 0;

    // Time since last sample
    uint64_t _timeSinceLastSampleUs = 0;
    uint64_t _minTimeBetweenSamplesUs = 0;

    // Enable/disable sampling
    bool _samplingEnabled = true;

    // Allocate at start
    bool _allocateAtStart = true;

    // Sample buffer
    std::vector<char, SpiramAwareAllocator<char>> _sampleBuffer;

    // Helpers
    RaftRetCode apiSample(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    bool writeToFile(const String& filename, bool append, String& errMsg);
    void writeToConsole();
};
