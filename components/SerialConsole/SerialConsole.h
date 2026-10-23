/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Serial Console
// Provides serial terminal access to REST API and diagnostics
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "ProtocolOverAscii.h"
#include "SpiramAwareAllocator.h"

class RestAPIEndpointManager;
class CommsCoreIF;
class CommsChannelMsg;
class APISourceInfo;

class SerialConsole : public RaftSysMod
{
public:
    SerialConsole(const char* pModuleName, RaftJsonIF& sysConfig);
    virtual ~SerialConsole();

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new SerialConsole(pModuleName, sysConfig);
    }
    
    // Get
    int getChar();
    uint32_t readBytes(uint8_t* pBuf, uint32_t maxLen);

    // Put
    void putStr(const char* pStr);
    void putStr(const String& str);

    // Get the state of the reception of Commands 
    // Maybe:
    //   idle = 'i' = no command entry in progress,
    //   newChar = XOFF = new command char received since last call - pause transmission
    //   waiting = 'w' = command incomplete but no new char since last call
    //   complete = XON = command completed - resume transmission
    typedef char CommandRxState;
    CommandRxState getXonXoff();

    // Defaults
    static const int DEFAULT_UART_NUM = 0;
    static const int DEFAULT_BAUD_RATE = 115200;
    static const int DEFAULT_RX_BUFFER_SIZE = 1024;
    // USB Serial/JTAG drops what arrives when its buffer is full, and the host sends as fast as USB allows, so a
    // host's protocol frames (e.g. a file upload's blocks) need room while the main loop is busy elsewhere
    static const int DEFAULT_USB_RX_BUFFER_SIZE = 4096;
    static const int DEFAULT_TX_BUFFER_SIZE = 4096;
    static const bool DEFAULT_CRLF_ON_TX = true;

protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;
    
    // Add comms channels
    virtual void addCommsChannels(CommsCoreIF& commsCore) override final;

    // Handle JSON command
    virtual RaftRetCode receiveCmdJSON(const char* cmdJSON, String* pResponseJSON = nullptr) override final;

public:
    // XON/XOFF hadnling
    static constexpr char ASCII_XOFF = 0x13;
    static constexpr char ASCII_XON = 0x11;

    // State
    static constexpr CommandRxState CommandRx_idle = 'i';
    static constexpr CommandRxState CommandRx_newChar = ASCII_XOFF;
    static constexpr CommandRxState CommandRx_waiting = 'w';
    static constexpr CommandRxState CommandRx_complete = ASCII_XON;

private:
    // isEnabled and isInitialised
    bool _isEnabled = false;
    bool _isInitialised = false;
    bool _uartDriverInstalled = false;

    // Port released to another owner (receiveCmdJSON "releasePort") - nothing is
    // read from or written to it until "reclaimPort".  Used when the port carries
    // something else for a while, e.g. a binary link on USB-Serial-JTAG.
    bool _portReleased = false;

    // CRLF string on tx line end
    bool _crlfOnTx = DEFAULT_CRLF_ON_TX;

#ifdef __linux__
    // Linux terminal settings
    void* _origTermios = nullptr;
#endif

    // Serial details
    int _uartNum = DEFAULT_UART_NUM;
    int _baudRate = DEFAULT_BAUD_RATE;

    // Buffer sizes
    uint32_t _rxBufferSize = DEFAULT_RX_BUFFER_SIZE;
    uint32_t _txBufferSize = DEFAULT_TX_BUFFER_SIZE;

    // Bytes to read in a loop call (read at once, and passed on as one message when they are protocol frames: a
    // byte or a USB packet at a time would make many small messages, which can fill the comms channel's queue)
    static const uint32_t MAX_BYTES_TO_PROCESS_IN_LOOP = 1024;

    // Bytes read but not yet processed (processing stops after a command line, to run it)
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _rxChunk;
    uint32_t _rxChunkPos = 0;

    // Procotol
    String _protocol;

    // Line being entered
    String _curLine;

    // Line limits
    static const int MAX_REGULAR_LINE_LEN = 100;
    static const int ABS_MAX_LINE_LEN = 1000;

    // Prev char entered (for line end checks)
    int _prevChar = -1;

    // Cur state (XON/XOFF etc)
    CommandRxState _cmdRxState = CommandRx_idle;

    // EndpointID used to identify this message channel to the CommsCoreIF
    uint32_t _commsChannelID;

    // ProtocolOverAscii to handle comms through this serial port with MSB set
    static const uint32_t PROTOCOL_OVER_ASCII_MSG_MAX_LEN = 1000;
    ProtocolOverAscii _protocolOverAscii;

    // Text for the USB-Serial-JTAG port that hasn't fitted in the driver's transmit buffer yet (e.g. a reply larger
    // than it): written from loop() as the host reads, without waiting. The driver takes a write whole or not at all,
    // so text is written in pieces of at most TX_PIECE_MAX_BYTES (which fit a buffer that has room), and later text
    // queues behind any already pending so the order is kept
    SpiramAwareUint8Vector _txPending;
    uint32_t _txPendingPos = 0;
    uint32_t _txPendingDropped = 0;
    static const uint32_t TX_PIECE_MAX_BYTES = 1024;
    static const uint32_t TX_PENDING_MAX_BYTES = 65536;
    void consoleWrite(const char* pText, uint32_t len);
    bool writePiecesNow(const char*& pText, uint32_t& len);
    void serviceTxPending();

    // Helpers
    void showEndpoints();
    bool sendMsg(CommsChannelMsg& msg);
    void processReceivedData(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& rxData);
    RaftRetCode apiConsole(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "SerialConsole";
};
