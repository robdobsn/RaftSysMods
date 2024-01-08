/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommandSerial
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include "RestAPIEndpointManager.h"
#include "CommsCoreIF.h"
#include "CommandSerialPort.h"
#include "CommsBridgeMsg.h"
#include "SysModBase.h"

class CommsChannelMsg;

class CommandSerial : public SysModBase
{
public:
    // Constructor/destructor
    CommandSerial(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~CommandSerial();

protected:
    // Setup
    virtual void setup() override final;

    // Service - called frequently
    virtual void service() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager &endpointManager) override final;

    // Add comms channels
    virtual void addCommsChannels(CommsCoreIF& commsCore) override final;

private:

    // List of serial ports
    std::list<CommandSerialPort> _serialPorts;

    // Comms core IF
    CommsCoreIF* _pCommsCoreIF = nullptr;

    // EndpointID used to identify this message channel to the CommsCoreIF
    uint32_t _commsChannelID = CommsCoreIF::CHANNEL_ID_UNDEFINED;
    uint32_t _bridgeID = COMMS_BRIDGE_ID_COM_SERIAL_0;

    // Helpers
    bool sendMsg(CommsChannelMsg& msg);
    RaftRetCode apiCommandSerial(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);
};
