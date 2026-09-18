/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLE Manager Stats
// Stats for BLE connection
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <stdint.h>
#include <atomic>
#include "MovingRate.h"

// Note on thread-safety: stats are updated on the NimBLE host task (rx) and on the task which sends (tx) and
// are read on the main task. The counters are atomic. The MovingRate objects are not thread-safe - each has
// a single writer and a reader on a different task may (rarely) see a transiently incorrect rate

class BLEManStats
{
public:
    BLEManStats()
    {
        clear();
    }

    void clear()
    {
        _rxMsgCount = 0;
        _txMsgCount = 0;
        _rxTotalBytes = 0;
        _txTotalBytes = 0;
        _txErrCount = 0;
        _rxTestFrameCount = 0;
        _rxTestFrameBytes = 0;
        _rxRate.clear();
        _txRate.clear();
        _txErrRate.clear();
        clearTestPerfStats();
    }
    void clearTestPerfStats()
    {
        _rxTestFrameRate.clear();
        _rxTestSeqErrs = 0;
        _rxTestDataErrs = 0;
    }

    void rxMsg(uint32_t msgSize)
    {
        _rxMsgCount++;
        _rxTotalBytes += msgSize;
        _rxRate.sample(_rxTotalBytes);
    }

    void txMsg(uint32_t msgSize, bool rslt)
    {
        _txMsgCount++;
        _txTotalBytes += msgSize;
        _txRate.sample(_txTotalBytes);
        _txErrCount += rslt ? 0 : 1;
        _txErrRate.sample(_txErrCount);
    }

    void rxTestFrame(uint32_t msgSize, bool seqOK, bool dataOK)
    {
        _rxTestFrameCount++;
        _rxTestFrameBytes += msgSize;
        _rxTestFrameRate.sample(_rxTestFrameBytes);
        _rxTestSeqErrs += seqOK ? 0 : 1;
        _rxTestDataErrs += dataOK ? 0 : 1;
    }

    String getJSON(bool includeBraces, bool shortForm) const
    {
        char buf[200];
        if (shortForm)
        {
            snprintf(buf, sizeof(buf), R"("rxBPS":%.1f,"txBPS":%.1f)",
                _rxRate.getRatePerSec(),
                _txRate.getRatePerSec());
        }
        else
        {
            snprintf(buf, sizeof(buf), R"("rxM":%d,"rxB":%d,"rxBPS":%.1f,"txM":%d,"txB":%d,"txBPS":%.1f,"txErr":%d,"txErrPS":%.1f)",
                (int)_rxMsgCount.load(),
                (int)_rxTotalBytes.load(),
                _rxRate.getRatePerSec(),
                (int)_txMsgCount.load(),
                (int)_txTotalBytes.load(),
                _txRate.getRatePerSec(),
                (int)_txErrCount.load(),
                _txErrRate.getRatePerSec());
        }
        String json = buf;
        if (_rxTestFrameCount.load() > 0)
        {
            snprintf(buf, sizeof(buf), R"("tM":%d,"tB":%d,"tBPS":%.1f,"tSeqErrs":%d,"tDatErrs":%d)",
                (int)_rxTestFrameCount.load(),
                (int)_rxTestFrameBytes.load(),
                _rxTestFrameRate.getRatePerSec(),
                (int)_rxTestSeqErrs.load(),
                (int)_rxTestDataErrs.load());
            json += ",";
            json += buf;
        }
        if (includeBraces)
        {
            return "{" + json + "}";
        }
        return json;
    }

    double getTestRate() const
    {
        return _rxTestFrameRate.getRatePerSec();
    }

    uint32_t getTestSeqErrCount() const
    {
        return _rxTestSeqErrs;
    }

    uint32_t getTestDataErrCount() const
    {
        return _rxTestDataErrs;
    }

private:
    // Regular messages
    std::atomic<uint32_t> _rxMsgCount{0};
    std::atomic<uint32_t> _txMsgCount{0};
    std::atomic<uint32_t> _rxTotalBytes{0};
    std::atomic<uint32_t> _txTotalBytes{0};
    std::atomic<uint32_t> _txErrCount{0};
    #define MOVING_AVERAGE_WINDOW_SIZE 5
    MovingRate<MOVING_AVERAGE_WINDOW_SIZE> _rxRate;
    MovingRate<MOVING_AVERAGE_WINDOW_SIZE> _txRate;
    MovingRate<MOVING_AVERAGE_WINDOW_SIZE> _txErrRate;

    // Test frames (used for performance testing)
    std::atomic<uint32_t> _rxTestFrameCount{0};
    std::atomic<uint32_t> _rxTestFrameBytes{0};
    std::atomic<uint32_t> _rxTestSeqErrs{0};
    std::atomic<uint32_t> _rxTestDataErrs{0};
    #define TEST_FRAME_WINDOW_SIZE 40
    MovingRate<TEST_FRAME_WINDOW_SIZE> _rxTestFrameRate;
};
