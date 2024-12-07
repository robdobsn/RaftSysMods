/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Command Serial Port
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include "RaftJsonIF.h"
#include "SpiramAwareAllocator.h"

class CommandSerialPort
{
public:
    CommandSerialPort();
    virtual ~CommandSerialPort();

    // Setup
    void setup(RaftJsonIF& config, const char* pModName);

    // Set and get commsChannelID
    uint32_t getChannelID() const { return _commsChannelID; }
    void setChannelID(uint32_t commsChannelID) { _commsChannelID = commsChannelID; }

    // Get protocol
    const String& getProtocol() const { return _protocol; }

    // Get name
    const String& getName() const { return _name; }

    // Get data
    bool getData(SpiramAwareUint8Vector &data);

    // Put data
    uint32_t putData(const uint8_t *pMsg, uint32_t msgLen);

    // Form port name default
    String formPortNameDefault(const char* baseName);

    // Bridging
    void setBridgeID(uint32_t bridgeID) { _bridgeID = bridgeID; _isBridged = true; }
    void clearBridgeID() { _bridgeID = 0; _isBridged = false; }
    bool isBridged() const { return _isBridged; }
    uint32_t getBridgeID() const { return _bridgeID; }

    // UART number
    int getUartNum() const { return _uartNum; }

private:
    // Vars
    bool _isEnabled = false;

    // Serial details
    int _uartNum = 0;
    int _baudRate = 921600;
    int _txPin = 0;
    int _rxPin = 0;
    uint32_t _rxBufSize = 1024;
    uint32_t _txBufSize = 1024;

    // Flag indicating begun
    bool _isInitialised = false;

    // Protocol and name
    String _protocol;
    String _name;

    // Comms channel ID
    uint32_t _commsChannelID = 0;

    // Bridge
    uint32_t _bridgeID = 0;
    bool _isBridged = false;

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "CmdSerPort";
};
