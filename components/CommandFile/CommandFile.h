/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommandFile
//
// Rob Dobson and Declan Shafi 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RestAPIEndpointManager.h"
#include "RaftSysMod.h"

class CommandFile : public RaftSysMod
{
public:
    // Constructor/destructor
    CommandFile(const char* pModuleName, RaftJsonIF& sysConfig);
    virtual ~CommandFile();

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new CommandFile(pModuleName, sysConfig);
    }
    
protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager &endpointManager) override final;

private:
    // Helpers
    void applySetup();
    RaftRetCode apiFileRun(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    //API Processing
    bool handleAPIFile(String& fileName);
    bool processAPILine();
    bool exec();

    //State control
    enum State
    {
        API_STATE_PROCESSING,
        API_STATE_SLEEPING,
        API_STATE_IDLE
    };

    State _curState;

    //Commands
    String _APICode;
    int _curLine;
    int _curPosition;
    int _repsLeft = 0;
    String _curCommand;

    static const int MAX_API_FILE_LENGTH = 5000;

    RestAPIEndpointManager* _pRestAPIEndpointManager;

    //Sleep
    long _stateTimerMilis;
    int _sleepTimeMilis;

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "CmdFile";
};

