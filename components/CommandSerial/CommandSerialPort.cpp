/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Command Serial Port
//
// Rob Dobson 2020-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "CommandSerialPort.h"
#include "RaftUtils.h"
#include "SpiramAwareAllocator.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// #define DEBUG_COMMAND_SERIAL_DETAIL

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommandSerialPort::CommandSerialPort()
{
}

CommandSerialPort::~CommandSerialPort()
{
    if (_isInitialised)
        uart_driver_delete((uart_port_t)_uartNum);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandSerialPort::setup(RaftJsonIF& config, const char* pModName)
{
    // Clear previous config if we've been here before
    if (_isInitialised)
        uart_driver_delete((uart_port_t)_uartNum);
    _isInitialised = false;

    // Enable
    _isEnabled = config.getLong("enable", 0) != 0;

    // Port
    _uartNum = config.getLong("uartNum", 1);

    // Baud
    _baudRate = config.getLong("baudRate", 921600);

    // Protocol
    _protocol = config.getString("protocol", "");

    // Name
    String defaultPortName = formPortNameDefault(pModName);
    _name = config.getString("name", defaultPortName.c_str());

    // Pins
    _rxPin = config.getLong("rxPin", -1);
    _txPin = config.getLong("txPin", -1);

    // Rx pullup
    bool rxPullup = config.getLong("rxPullup", 0);

    // Buffers
    _rxBufSize = config.getLong("rxBufSize", 1024);
    _txBufSize = config.getLong("txBufSize", 1024);

    // Setup
    if (_isEnabled && (_rxPin != -1) && (_txPin != -1))
    {

        // Configure UART. Note that REF_TICK is used so that the baud rate remains
        // correct while APB frequency is changing in light sleep mode
        const uart_config_t uart_config = {
                .baud_rate = _baudRate,
                .data_bits = UART_DATA_8_BITS,
                .parity = UART_PARITY_DISABLE,
                .stop_bits = UART_STOP_BITS_1,
                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                .rx_flow_ctrl_thresh = 10,
#if ESP_IDF_VERSION <= ESP_IDF_VERSION_VAL(5, 0, 0)
                .use_ref_tick = false,
#else
                .source_clk = UART_SCLK_DEFAULT,
#endif
        };
        esp_err_t err = uart_param_config((uart_port_t)_uartNum, &uart_config);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "Failed to initialize uart %s param config uartNum %d baudRate %d err %d", 
                            _name.c_str(), _uartNum, _baudRate, err);
            return;
        }

        // Setup pins
        err = uart_set_pin((uart_port_t)_uartNum, _txPin, _rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "Failed to set uart %s pins uartNum %d txPin %d rxPin %d err %d", 
                            _name.c_str(), _uartNum, _txPin, _rxPin, err);
            return;
        }

        if (rxPullup)
            gpio_pullup_en((gpio_num_t)_rxPin);

        // Delay before UART change
        vTaskDelay(1);

        // Install UART driver for interrupt-driven reads and writes
        err = uart_driver_install((uart_port_t)_uartNum, _rxBufSize, _txBufSize, 0, NULL, 0);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "Failed to install uart %s driver, uartNum %d rxBufSize %d txBufSize %d err %d", 
                            _name.c_str(), _uartNum, (int)_rxBufSize, (int)_txBufSize, err);
            return;
        }

        // Init ok
        _isInitialised = true;

        // Log
        LOG_I(MODULE_PREFIX, "setup ok %s uartNum %d baudRate %d txPin %d rxPin %d%s rxBufSize %d txBufSize %d protocol %s", 
                    _name.c_str(), _uartNum, _baudRate, _txPin, _rxPin, rxPullup ? "(pullup)" : "", 
                    (int)_rxBufSize, (int)_txBufSize, _protocol.c_str());
    } else {
        LOG_I(MODULE_PREFIX, "setup %s enabled %s uartNum %d txPin %d rxPin %d", 
                    _name.c_str(), _isEnabled ? "YES" : "NO", _uartNum, _txPin, _rxPin);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommandSerialPort::getData(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& data)
{
    // Check if initialised
    if (!_isInitialised)
        return false;

    // Check anything available
    static const int MAX_BYTES_PER_CALL = 2000;
    size_t numCharsAvailable = 0;
    esp_err_t err = uart_get_buffered_data_len((uart_port_t)_uartNum, &numCharsAvailable);
    if ((err == ESP_OK) && (numCharsAvailable > 0))
    {
        // Get data
        uint32_t bytesToGet = numCharsAvailable < MAX_BYTES_PER_CALL ? numCharsAvailable : MAX_BYTES_PER_CALL;
        data.resize(bytesToGet);        
        uint32_t bytesRead = uart_read_bytes((uart_port_t)_uartNum, data.data(), bytesToGet, 1);
        if (bytesRead != 0)
        {
            data.resize(bytesRead);

#ifdef DEBUG_COMMAND_SERIAL_DETAIL
            String outStr;
            Raft::getHexStrFromBytes(data.data(), data.size(), outStr);
            LOG_I(MODULE_PREFIX, "getData uartNum %d dataLen %d data %s", _uartNum, data.size(), outStr.c_str());
#endif
            // LOG_D(MODULE_PREFIX, "loop charsAvail %d ch %02x", numCharsAvailable, buf[0]);
            return true;
        }
    }

    // No data
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Put data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t CommandSerialPort::putData(const uint8_t* pData, uint32_t dataLen)
{
    // Check if initialised
    if (!_isInitialised)
        return 0;

#ifdef DEBUG_COMMAND_SERIAL_DETAIL
    // Debug
    String outStr;
    Raft::getHexStrFromBytes(pData, dataLen, outStr);
    LOG_I(MODULE_PREFIX, "putData uartNum %d dataLen %d data %s", _uartNum, dataLen, outStr.c_str());
#endif

    // Send the message
    return uart_write_bytes((uart_port_t)_uartNum, (const char*)pData, dataLen);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Form default port name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String CommandSerialPort::formPortNameDefault(const char* baseName)
{
    String defaultPortName = baseName ? String(baseName) : String("Serial");
    defaultPortName += String("_") + String(_uartNum);
    return defaultPortName;
}
