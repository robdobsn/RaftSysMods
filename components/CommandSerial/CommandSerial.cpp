/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommandSerial
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "CommandSerial.h"
#include "RaftUtils.h"
#include "CommsChannelMsg.h"
#include "CommsChannelSettings.h"

#define WARN_ON_COMMAND_SERIAL_TX_NOT_FOUND

// #define DEBUG_COMMAND_SERIAL
// #define DEBUG_COMMAND_SERIAL_RX
// #define DEBUG_COMMAND_SERIAL_TX

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommandSerial::CommandSerial(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
}

CommandSerial::~CommandSerial()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSerial::setup()
{
    // Get length of serial port configs array
    int arrayLen = 0;
    modConfig().getType("ports", arrayLen);

    // Check valid
    if (arrayLen <= 0)
        return;

    // Check maximum number of ports
    if (arrayLen > MAX_SERIAL_PORTS)
    {
        LOG_W(MODULE_PREFIX, "setup too many serial ports %d > %d", arrayLen, MAX_SERIAL_PORTS);
        arrayLen = MAX_SERIAL_PORTS;
    }

    // Clear list of ports
    _serialPorts.clear();
    _serialPorts.resize(arrayLen);

    // Iterate through serial port configs creating ports
    for (int i = 0; i < arrayLen; i++)
    {
        // Configure the port
        RaftJsonPrefixed portConfig(modConfig(), "ports[" + String(i) + "]");
        _serialPorts.back().setup(portConfig, modName());
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop (called frequently)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSerial::loop()
{
    // Check comms channel manager
    if (!_pCommsCoreIF)
        return;

    // Iterate through serial ports
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> charBuf;
    for (auto& serialPort : _serialPorts)
    {
        // Get received data
        if (serialPort.getData(charBuf))
        {
            // Handle data
            if (charBuf.size() > 0)
            {
                // Send to comms channel
                _pCommsCoreIF->inboundHandleMsg(serialPort.getChannelID(), charBuf.data(), charBuf.size());

#ifdef DEBUG_COMMAND_SERIAL_RX
                // Debug
                LOG_I(MODULE_PREFIX, "loop channelID %d, len %d", serialPort.getChannelID(), charBuf.size());
#endif
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSerial::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    endpointManager.addEndpoint("commandserial", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                    std::bind(&CommandSerial::apiCommandSerial, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                    "commandserial API e.g. commandserial/bridge/setup?port=Serial1&name=Bridge1&idleCloseSecs=10 or commandserial/bridge/remove?id=1&force=0");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Comms channels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSerial::addCommsChannels(CommsCoreIF& commsCoreIF)
{
    // Store comms channel manager
    _pCommsCoreIF = &commsCoreIF;

    // Comms channel
    static const CommsChannelSettings commsChannelSettings;

    // Register each port as a message channel
    for (auto& serialPort : _serialPorts)
    {
        // Register the channel
        uint32_t channelID = commsCoreIF.registerChannel(
                serialPort.getProtocol().c_str(),
                modName(),
                serialPort.getName().c_str(),
                std::bind(&CommandSerial::sendMsg, this, std::placeholders::_1),
                [this](uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn) {
                    return true;
                },
                &commsChannelSettings);

        // Set the channel ID
        serialPort.setChannelID(channelID);

        // Debug
#ifdef DEBUG_COMMAND_SERIAL
        LOG_I(MODULE_PREFIX, "addCommsChannels channelID %d name %s uart %d",
                channelID, serialPort.getName().c_str(), serialPort.getUartNum());
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommandSerial::sendMsg(CommsChannelMsg& msg)
{
    // Debug
#ifdef DEBUG_COMMAND_SERIAL_TX
    LOG_I(MODULE_PREFIX, "sendMsg channelID %d, msgType %s msgNum %d, len %d",
            msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen());
#endif

    // Find the port
    for (auto& serialPort : _serialPorts)
    {
        if (serialPort.getChannelID() == msg.getChannelID())
        {
            // Send the message
            uint32_t bytesSent = serialPort.putData(msg.getBuf(), msg.getBufLen());

            // Check for error
            if (bytesSent != msg.getBufLen())
            {
                LOG_W(MODULE_PREFIX, "sendMsg channelID %d, msgType %s msgNum %d, len %d only wrote %d bytes",
                        msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen(), bytesSent);
            }
            else
            {
                return true;
            }
        }
    }

    // Not found
#ifdef WARN_ON_COMMAND_SERIAL_TX_NOT_FOUND
    LOG_W(MODULE_PREFIX, "sendMsg channelID %d not found", msg.getChannelID());
#endif

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode CommandSerial::apiCommandSerial(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Check valid
    if (!_pCommsCoreIF)
    {
        LOG_W(MODULE_PREFIX, "apiCommandSerial noCommsCore");
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "noCommsCore", nullptr, RaftRetCode::RAFT_INVALID_OBJECT);
    }

    // Extract parameters
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson nvJson = RaftJson::getJSONFromNVPairs(nameValues, true);

    // Check valid
    if (params.size() < 3)
    {
        LOG_W(MODULE_PREFIX, "apiCommandSerial not enough params %d", params.size());
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "notEnoughParams", nullptr, RaftRetCode::RAFT_INVALID_DATA);
    }

    // Check type of command
    String cmdStr = params[1];
    if (cmdStr.equalsIgnoreCase("bridge"))
    {
        // Check if setting up bridge
        if (params[2].equalsIgnoreCase("setup"))
        {

            // Get the port
            String portName = nvJson.getString("port", "");
            if (portName.length() == 0)
            {
                LOG_W(MODULE_PREFIX, "apiCommandSerial no port");
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "noPort", nullptr, RaftRetCode::RAFT_INVALID_DATA);
            }

            // Find the port
            for (auto& serialPort : _serialPorts)
            {
                if (serialPort.getName().equalsIgnoreCase(portName))
                {
                    // Get bridge name
                    String bridgeName = nvJson.getString("name", ("Bridge_" + serialPort.getName()).c_str());

                    // Get idle close time (0 = default)
                    uint32_t idleCloseSecs = nvJson.getLong("idleCloseSecs", 0);

                    // Register the bridge channel
                    uint32_t bridgeID = _pCommsCoreIF->bridgeRegister(bridgeName.c_str(), sourceInfo.channelID, 
                            serialPort.getChannelID(), idleCloseSecs);

                    // Set the bridge ID
                    serialPort.setBridgeID(bridgeID);

                    // Set result
                    String resultStr = String("\"bridgeID\":") + String(bridgeID);
                    Raft::setJsonResult(reqStr.c_str(), respStr, true, nullptr, resultStr.c_str());
                    return RaftRetCode::RAFT_OK;
                }
            }

            // If we get here the port name isn't found
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "portNotFound", nullptr, RaftRetCode::RAFT_INVALID_DATA);
        }

        // Check if removing bridge
        else if (params[2].equalsIgnoreCase("remove"))
        {
            // Get the bridge ID
            uint32_t bridgeID = nvJson.getLong("id", 0);

            // Check if close forced
            bool forceClose = nvJson.getLong("force", 0) != 0;

            // Find the port
            for (auto& serialPort : _serialPorts)
            {
                if (serialPort.isBridged() && (serialPort.getBridgeID() == bridgeID))
                {
                    // Unregister the bridge
                    _pCommsCoreIF->bridgeUnregister(bridgeID, forceClose);

                    // Remove the bridge
                    serialPort.clearBridgeID();

                    // Set result
                    return Raft::setJsonResult(reqStr.c_str(), respStr, true);
                }
            }

            // If we get here the bridge name isn't found
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "bridgeIDNotFound", nullptr, RaftRetCode::RAFT_INVALID_DATA);
        }

        // Unknown bridge action
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "unknownAction", nullptr, RaftRetCode::RAFT_INVALID_DATA);
    }

    // Unknown command
    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "unknownCommand", nullptr, RaftRetCode::RAFT_INVALID_DATA);
}
