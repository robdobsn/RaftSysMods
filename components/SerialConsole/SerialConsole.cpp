/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Serial Console
// Provides serial terminal access to REST API and diagnostics
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "SerialConsole.h"
#include "RestAPIEndpointManager.h"
#include "CommsChannelSettings.h"
#include "CommsCoreIF.h"
#include "RaftUtils.h"
#include "CommsChannelMsg.h"

#ifdef __linux__
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <cstring>
#else
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "driver/uart.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "driver/usb_serial_jtag_vfs.h"
#endif

#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif
#endif // __linux__

#define DEBUG_SERIAL_CONSOLE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SerialConsole::SerialConsole(const char* pModuleName, RaftJsonIF& sysConfig)
        : RaftSysMod(pModuleName, sysConfig)
{
    _curLine.reserve(MAX_REGULAR_LINE_LEN);

    // ChannelID
    _commsChannelID = CommsCoreIF::CHANNEL_ID_UNDEFINED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SerialConsole::~SerialConsole()
{
#ifdef __linux__
    // Restore terminal settings on Linux
    if (_origTermios)
    {
        struct termios* pOrigTermios = static_cast<struct termios*>(_origTermios);
        tcsetattr(STDIN_FILENO, TCSANOW, pOrigTermios);
        delete pOrigTermios;
        _origTermios = nullptr;
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SerialConsole::setup()
{
    // Get params
    _isEnabled = configGetBool("enable", false);
    _crlfOnTx = configGetLong("crlfOnTx", DEFAULT_CRLF_ON_TX);
    _uartNum = configGetLong("uartNum", DEFAULT_UART_NUM);
    _baudRate = configGetLong("baudRate", 0);
    _rxBufferSize = configGetLong("rxBuf", DEFAULT_RX_BUFFER_SIZE);
    _txBufferSize = configGetLong("txBuf", DEFAULT_TX_BUFFER_SIZE);

    // Protocol
    _protocol = configGetString("protocol", "RICSerial");

#ifdef __linux__

    LOG_I(MODULE_PREFIX, "setup called for Linux, enabled %s", _isEnabled ? "YES" : "NO");

    // Setup Linux terminal for raw mode if enabled
    if (_isEnabled)
    {
        // Allocate termios structure
        struct termios* pOrigTermios = new struct termios();
        _origTermios = pOrigTermios;

        // Check if stdin is a terminal
        if (!isatty(STDIN_FILENO))
        {
            LOG_I(MODULE_PREFIX, "setup - stdin is not a terminal, skipping raw mode setup");
            delete pOrigTermios;
            _origTermios = nullptr;
            _isInitialised = true;
            return;
        }

        // Save original terminal settings
        if (tcgetattr(STDIN_FILENO, pOrigTermios) != 0)
        {
            LOG_W(MODULE_PREFIX, "setup failed to get terminal attributes");
            delete pOrigTermios;
            _origTermios = nullptr;
            return;
        }

        // Configure raw mode
        struct termios raw = *pOrigTermios;

        // Disable canonical mode (line buffering) and echo
        raw.c_lflag &= ~(ECHO | ICANON);

        // Non-blocking read: return immediately if no data
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        // Apply settings
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        {
            LOG_W(MODULE_PREFIX, "setup failed to set terminal attributes");
            delete pOrigTermios;
            _origTermios = nullptr;
            return;
        }

        LOG_I(MODULE_PREFIX, "setup OK - Linux terminal configured for raw mode enabled %s crlfOnTx %s",
                    _isEnabled ? "YES" : "NO", _crlfOnTx ? "YES" : "NO");
    }

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)

    // Configure USB-JTAG if required
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
                .tx_buffer_size = _txBufferSize,
                .rx_buffer_size = _rxBufferSize,
                };

    esp_err_t jtagErr = usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    if (jtagErr != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED can't install jtag driver, err %d", jtagErr);
        return;
    }

    // Tell vfs to use the driver
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0) 
    usb_serial_jtag_vfs_use_driver();
#endif

    // Debug
    LOG_I(MODULE_PREFIX, "setup USB JTAG OK enabled %s rxBufLen %d txBufLen %d", 
                _isEnabled ? "YES" : "NO", _rxBufferSize, _txBufferSize);

#else

    // Config required if baud rate specified
    bool configRequired = _baudRate != 0;

    // Check for interrupt allocation flags (IRAM)
    int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    // Install UART driver for interrupt-driven reads and writes
    esp_err_t err = uart_driver_install((uart_port_t)_uartNum, _rxBufferSize, _txBufferSize, 0, 
                            NULL, intr_alloc_flags);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED uartNum %d can't install uart driver, err %d", _uartNum, err);
        return;
    }

    // Check if a config required
    if (configRequired)
    {
        if (_baudRate != DEFAULT_BAUD_RATE)
        {
            LOG_I(MODULE_PREFIX, "Changing uartNum %d baud rate to %d", _uartNum, _baudRate);
            vTaskDelay(10);
        }

        // Delay before UART change
        vTaskDelay(1);

        // Configure UART. Note that REF_TICK is used so that the baud rate remains
        // correct while APB frequency is changing in light sleep mode
        const uart_config_t uart_config = {
                .baud_rate = _baudRate,
                .data_bits = UART_DATA_8_BITS,
                .parity = UART_PARITY_DISABLE,
                .stop_bits = UART_STOP_BITS_1,
                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                .rx_flow_ctrl_thresh = 100,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
                .use_ref_tick = true,
#else
                .source_clk = UART_SCLK_DEFAULT,
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 1)
                .flags = {},
#endif
        };
        esp_err_t err = uart_param_config((uart_port_t)_uartNum, &uart_config);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "setup FAILED uartNum %d can't initialize uart, err %d", _uartNum, err);
            return;
        }

        // Delay after UART change
        vTaskDelay(1);
    }

    // Debug
    LOG_I(MODULE_PREFIX, "setup OK enabled %s uartNum %d crlfOnTx %s rxBufLen %d txBufLen %d", 
                _isEnabled ? "YES" : "NO", _uartNum, _crlfOnTx ? "YES" : "NO",
                _rxBufferSize, _txBufferSize);

#endif // CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG

    _isInitialised = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SerialConsole::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    endpointManager.addEndpoint("console", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                std::bind(&SerialConsole::apiConsole, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                "console API e.g. console/settings?baud=1000000");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Comms channels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SerialConsole::addCommsChannels(CommsCoreIF& commsCore)
{
    // Comms channel
    static CommsChannelSettings commsChannelSettings;

    // Register as a channel of protocol messages
    _commsChannelID = commsCore.registerChannel(_protocol.c_str(),
            modName(),
            modName(),
            std::bind(&SerialConsole::sendMsg, this, std::placeholders::_1),
            [this](uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn) {
                return true;
            },
            &commsChannelSettings);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get from terminal
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int SerialConsole::getChar()
{
    if (_isEnabled)
    {
#ifdef __linux__
        // Linux: read from stdin
        uint8_t ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n == 1)
        {
#ifdef DEBUG_SERIAL_CONSOLE
            LOG_I(MODULE_PREFIX, "getChar %02x", ch);
#endif
            return ch;
        }
        return -1;  // No data available
#else
        // ESP32: Check anything available
        size_t numCharsAvailable = 0;
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        esp_err_t err = ESP_OK;
        numCharsAvailable = 1;
#else
        esp_err_t err = uart_get_buffered_data_len((uart_port_t)_uartNum, &numCharsAvailable);
#endif
        if ((err == ESP_OK) && (numCharsAvailable > 0))
        {
            // Get char
            uint8_t charRead;
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
            if (usb_serial_jtag_read_bytes(&charRead, 1, 0) > 0)
#else
            if (uart_read_bytes((uart_port_t)_uartNum, &charRead, 1, 0) > 0)
#endif
            {
                // Debug
#ifdef DEBUG_SERIAL_CONSOLE
                LOG_I(MODULE_PREFIX, "getChar %02x", charRead);
#endif
                return charRead;
            }
        }
#endif // __linux__
    }
    else
    {
        LOG_W(MODULE_PREFIX, "getChar called when not enabled");
    }
    return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Put to terminal
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SerialConsole::putStr(const char* pStr)
{
    if (_isEnabled && pStr)
    {
#ifdef __linux__
        // Linux: write to stdout
        write(STDOUT_FILENO, pStr, strlen(pStr));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
        // LOG_I(MODULE_PREFIX, "putStr strlen %d", strnlen(pStr, _txBufferSize/2+1));
        usb_serial_jtag_write_bytes((const char*)pStr, strnlen(pStr, _txBufferSize/2+1), 1);
#else
        uart_write_bytes((uart_port_t)_uartNum, pStr, strnlen(pStr, _txBufferSize/2+1));
#endif
    }
}

void SerialConsole::putStr(const String& str)
{
    if (_isEnabled)
    {
#ifdef __linux__
        // Linux: write to stdout
        write(STDOUT_FILENO, str.c_str(), str.length());
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
        // LOG_I(MODULE_PREFIX, "putString strlen %d", str.length());
        usb_serial_jtag_write_bytes(str.c_str(), str.length(), 1);
#else
        uart_write_bytes((uart_port_t)_uartNum, str.c_str(), str.length());
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the state of the reception of Commands 
// Maybe:
//   idle = 'i' = no command entry in progress,
//   newChar = XOFF = new command char received since last call - pause transmission
//   waiting = 'w' = command incomplete but no new char since last call
//   complete = XON = command completed - resume transmission
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SerialConsole::CommandRxState SerialConsole::getXonXoff()
{
    char curSt = _cmdRxState;
    if (_cmdRxState == CommandRx_complete)
    {
        // Serial.printf("<COMPLETE>");
        _cmdRxState = CommandRx_idle;
    }
    else if (_cmdRxState == CommandRx_newChar)
    {
        // Serial.printf("<NEWCH>");
        _cmdRxState = CommandRx_waiting;
    }
    return curSt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop (called frequently)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SerialConsole::loop()
{
    // Process received data
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> inboundMessage;
    for (uint32_t chIdx = 0; chIdx < MAX_BYTES_TO_PROCESS_IN_LOOP; chIdx++)
    {
        // Check for char
        int ch = getChar();
        if (ch == -1)
            break;

        // Check for MSB set
        if (ch >= 128)
        {
            // LOG_I(MODULE_PREFIX, "MSB set on rx %02x", ch);
            int decodedByte = _protocolOverAscii.decodeByte(ch);
            if (decodedByte != -1)
            {
                inboundMessage.push_back((uint8_t)decodedByte);
                // LOG_I(MODULE_PREFIX, "byte rx %02x", rxBuf[0]);
            }
            continue;
        }

        // Check for line end
        if ((ch == '\r') || (ch == '\n'))
        {
            // Check for terminal sending a CRLF sequence
            if (_prevChar == '\r')
            {
                _prevChar = ' ';
                continue;
            }
            _prevChar = ch;

            // Check if empty line - show menu
            if (_curLine.length() <= 0)
            {
                // Show endpoints
                showEndpoints();
                break;
            }

            putStr(_crlfOnTx ? "\r\n" : "\n");
            // Check for immediate instructions
#ifdef DEBUG_SERIAL_CONSOLE
            LOG_I(MODULE_PREFIX, "CommsSerial: ->cmdInterp cmdStr %s", _curLine.c_str());
#endif
            String retStr;
            if (getRestAPIEndpointManager())
                getRestAPIEndpointManager()->handleApiRequest(_curLine.c_str(), retStr, 
                                APISourceInfo(RestAPIEndpointManager::CHANNEL_ID_SERIAL_CONSOLE));
            // Display response
            putStr(retStr);
            putStr(_crlfOnTx ? "\r\n" : "\n");

            // Reset line
            _curLine = "";
            _cmdRxState = CommandRx_complete;
            break;
        }

        // Store previous char for CRLF checks
        _prevChar = ch;

        // Check line not too long
        if (_curLine.length() >= ABS_MAX_LINE_LEN)
        {
            _curLine = "";
            _cmdRxState = CommandRx_idle;
            continue;
        }

        // Check for backspace
        if (ch == 0x08)
        {
            if (_curLine.length() > 0)
            {
                _curLine.remove(_curLine.length() - 1);
                char tmpStr[4] = { (char)ch, ' ', (char)ch, '\0'};
                putStr(tmpStr);
            }
            continue;
        }
        else if (ch == '?')
        {
            // Check if empty line - show menu
            if (_curLine.length() <= 0)
            {
                // Show endpoints
                showEndpoints();
                break;
            }
        }

        // Output for user to see
        if (_curLine.length() == 0)
            putStr(_crlfOnTx ? "\r\n" : "\n");
        char tmpStr[2] = {(char)ch, '\0'};
        putStr(tmpStr);

        // Add char to line
        _curLine.concat((char)ch);

        // Set state to show we're busy getting a command
        _cmdRxState = CommandRx_newChar;
    }

    // Process any message received
    processReceivedData(inboundMessage);
}

void SerialConsole::processReceivedData(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& rxData)
{
    if (rxData.size() == 0)
        return;
    if (getCommsCore())
        getCommsCore()->inboundHandleMsg(_commsChannelID, rxData.data(), rxData.size());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// showEndpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SerialConsole::showEndpoints()
{
    if (!getRestAPIEndpointManager())
        return;
    for (int i = 0; i < getRestAPIEndpointManager()->getNumEndpoints(); i++)
    {
        RestAPIEndpoint* pEndpoint = getRestAPIEndpointManager()->getNthEndpoint(i);
        if (!pEndpoint)
            continue;
        putStr(String(" ") + pEndpoint->_endpointStr + String(": ") +  pEndpoint->_description + (_crlfOnTx ? "\r\n" : "\n"));
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool SerialConsole::sendMsg(CommsChannelMsg& msg)
{
    // Debug
    // LOG_I(MODULE_PREFIX, "sendMsg channelID %d, msgType %s msgNum %d, len %d",
    //         msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen());

    // Check valid
    if (!_isInitialised)
        return false;

    // Encode
    uint32_t encodedFrameMax = msg.getBufLen() * 2 > PROTOCOL_OVER_ASCII_MSG_MAX_LEN ? msg.getBufLen() * 2 : PROTOCOL_OVER_ASCII_MSG_MAX_LEN;
    uint8_t encodedFrame[encodedFrameMax];
    uint32_t encodedFrameLen = _protocolOverAscii.encodeFrame(msg.getBuf(), msg.getBufLen(), encodedFrame, encodedFrameMax);

    // Send the message
#ifdef __linux__
    int bytesSent = write(STDOUT_FILENO, (const char*)encodedFrame, encodedFrameLen);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    int bytesSent = usb_serial_jtag_write_bytes((const char*)encodedFrame, encodedFrameLen, 1);
#else
    int bytesSent = uart_write_bytes((uart_port_t)_uartNum, (const char*)encodedFrame, encodedFrameLen);
#endif
    if (bytesSent != encodedFrameLen)
    {
        LOG_W(MODULE_PREFIX, "sendMsg channelID %d, msgType %s msgNum %d, len %d only wrote %d bytes",
                msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), encodedFrameLen, bytesSent);

        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle JSON command
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SerialConsole::receiveCmdJSON(const char* cmdJSON)
{
#if !defined(__linux__) && !defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    // Extract command from JSON
    RaftJson jsonInfo(cmdJSON);
    String cmd = jsonInfo.getString("cmd", "");
    int baudRate = jsonInfo.getLong("baudRate", -1);
    int txBufSize = jsonInfo.getLong("txBuf", -1);
    int rxBufSize = jsonInfo.getLong("rxBuf", -1);
    if (cmd.equalsIgnoreCase("set"))
    {
        if (baudRate >= 0)
        {
            // Set UART baud rate
            uart_set_baudrate((uart_port_t)_uartNum, baudRate);
            LOG_W(MODULE_PREFIX, "receiveCmdJson baudRate (uart %d) changed to %d", _uartNum, baudRate);
        }
        if ((txBufSize > 0) || (rxBufSize > 0))
        {
            if (txBufSize > 0)
                _txBufferSize = txBufSize;
            if (rxBufSize > 0)
                _rxBufferSize = rxBufSize;

            // Remove existing driver
            esp_err_t err = uart_driver_delete((uart_port_t)_uartNum);
            if (err != ESP_OK)
            {
                LOG_E(MODULE_PREFIX, "receiveCmdJson FAILED to remove uart driver from port %d, err %d", _uartNum, err);
                return RAFT_INVALID_DATA;
            }

            // Install uart driver
            err = uart_driver_install((uart_port_t)_uartNum, _rxBufferSize, _txBufferSize, 0, NULL, 0);
            if (err != ESP_OK)
            {
                LOG_E(MODULE_PREFIX, "receiveCmdJson FAILED to install uart driver to port %d, err %d", _uartNum, err);
                return RAFT_INVALID_DATA;
            }
        }
        return RAFT_OK;
    }
#endif
    return RAFT_INVALID_OPERATION;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SerialConsole::apiConsole(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Extract parameters
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson nvJson = RaftJson::getJSONFromNVPairs(nameValues, true);

    // Check valid
    if (params.size() < 2)
    {
        Raft::setJsonErrorResult(reqStr.c_str(), respStr, "notEnoughParams");
        LOG_W(MODULE_PREFIX, "apiConsole not enough params %d", (int)params.size());
        return RaftRetCode::RAFT_INVALID_DATA;
    }

    // Check type of command
    String cmdStr = params[1];
    if (cmdStr.equalsIgnoreCase("settings"))
    {
#if !defined(__linux__) && !defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
        // Iterate over name/values
        RaftRetCode result = RaftRetCode::RAFT_INVALID_DATA;
        for (auto& nv : nameValues)
        {
            if (nv.name.equalsIgnoreCase("baud"))
            {
                // Set UART baud rate
                int baudRate = nv.value.toInt();
                LOG_I(MODULE_PREFIX, "apiConsole baudRate (uart %d) changed to %d", _uartNum, baudRate);

                // Delay a short time to help with serial comms
                delay(100);

                // Set baud rate
                uart_set_baudrate((uart_port_t)_uartNum, baudRate);
                result = Raft::setJsonResult(reqStr.c_str(), respStr, true);
                if (result != RaftRetCode::RAFT_OK)
                    break;
            }
        }
        if (result != RaftRetCode::RAFT_INVALID_DATA)
            return result;
#endif
    }

    // Unknown command
    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "unknownCommand");
}
