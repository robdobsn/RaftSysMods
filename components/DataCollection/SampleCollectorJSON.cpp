/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SampleCollector
// Collects samples in memory
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "SampleCollectorJSON.h"

static const char* MODULE_PREFIX = "SampleCollector";

// #define DEBUG_ADD_SAMPLE
// #define DEBUG_WRITE_TO_FILE

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
void SampleCollectorJSON::setup()
{
    // Get settings
    _sampleRateLimitHz = config.getLong("rateLimHz", 0);
    _maxTotalJSONStringSize = config.getLong("maxJsonLen", 0);
    _sampleHeader = config.getString("jsonHdr", "");
    _sampleAPIName = config.getString("apiName", "");
    _allocateAtStart = config.getBool("allocAtStart", true);
    _dumpToConsoleWhenFull = config.getBool("dumpToConsole", false);
    _dumpToFileName = config.getString("dumpToFile", "");
    _maxFileSize = config.getLong("maxFileSize", 0);

    // Settings
    if (_sampleRateLimitHz > 0)
        _minTimeBetweenSamplesUs = 1000000 / _sampleRateLimitHz;

    // Debug
    LOG_I(MODULE_PREFIX, "setup sampleRateLimitHz %d maxTotalJSONStringSize %d sampleHeader %s sampleAPIName %s allocateAtStart %s dumpToConsole %d dumpToFileName %s maxFileSize %d",
                _sampleRateLimitHz, 
                _maxTotalJSONStringSize, 
                _sampleHeader.c_str(), 
                _sampleAPIName.c_str(), 
                _allocateAtStart ? "Y" : "N", 
                _dumpToConsoleWhenFull,
                _dumpToFileName.c_str(),
                _maxFileSize);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add endpoints
/// @param pEndpoints Endpoint manager
void SampleCollectorJSON::addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints)
{
    // Check if API name defined
    if (_sampleAPIName.length() == 0)
        return;
    // Add endpoint for sampling
    pEndpoints.addEndpoint(_sampleAPIName.c_str(), 
            RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                        std::bind(&SampleCollectorJSON::apiSample, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        "handle samples, e.g. sample/start, sample/stop, sample/clear, sample/write/<filename>");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API
/// @param reqStr Request string
/// @param respStr Response string
/// @param sourceInfo Source info
RaftRetCode SampleCollectorJSON::apiSample(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Extract params
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    String paramsJSON = RaftJson::getJSONFromNVPairs(nameValues, true);

    // Handle commands
    bool rslt = true;
    String rsltStr;
    if (params.size() > 0)
    {
        // Start
        if (params[1].equalsIgnoreCase("start"))
        {
            _samplingEnabled = true;
            rsltStr = "Ok";
        }
        // Stop
        else if (params[1].equalsIgnoreCase("stop"))
        {
            _samplingEnabled = false;
            rsltStr = "Ok";
        }
        // Clear buffer
        else if (params[1].equalsIgnoreCase("clear"))
        {
            _sampleBuffer.clear();
            rsltStr = "Ok";
        }
        // Write to file
        else if (params[1].equalsIgnoreCase("write"))
        {
            rslt = writeToFile(params[2], false, rsltStr);
        }
        // Get buffer
        else if (params[1].equalsIgnoreCase("get"))
        {
            respStr = String(_sampleBuffer.data(), _sampleBuffer.size());
            _sampleBuffer.clear();
            return RAFT_OK;
        }
    }
    // Result
    if (rslt)
    {
        // LOG_I(MODULE_PREFIX, "apiSample: reqStr %s rslt %s", reqStr.c_str(), rsltStr.c_str());
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
    }
    // LOG_E(MODULE_PREFIX, "apiSample: FAILED reqStr %s rslt %s", reqStr.c_str(), rsltStr.c_str());
    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, rsltStr.c_str());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Write to file
/// @param filename Filename
/// @param errMsg Error message
bool SampleCollectorJSON::writeToFile(const String& filename, bool append, String& errMsg)
{
    // Get file size
    uint32_t fileSizeStart = 0;
    bool isFsOkStart = fileSystem.getFileInfo("", filename, fileSizeStart);

    // Open file
    FILE* pFile = fileSystem.fileOpen("", filename, true, 0, append);
    if (!pFile)
    {
        errMsg = "failOpen";
        return false;
    }

    // Write header
    bool rslt = true;
    if ((!append || (fileSizeStart == 0)) && !_sampleHeader.isEmpty())
    {
        uint32_t bytesWritten = fileSystem.fileWrite(pFile, (uint8_t*)(_sampleHeader + "\n").c_str(), _sampleHeader.length()+1);
        if (bytesWritten != _sampleHeader.length() + 1)
        {
            LOG_E(MODULE_PREFIX, "writeToFile FAILED header bytesWritten %d hdr(inc term) %d fileSize %d isFsOk %s", 
                        bytesWritten, _sampleHeader.length()+1,
                        fileSizeStart, isFsOkStart ? "Y" : "N");
            errMsg = "failWriteHdr";
            rslt = false;
        }
    }

    // Write buffer
    if (rslt)
    {
        uint32_t bytesWritten = fileSystem.fileWrite(pFile, (uint8_t*)_sampleBuffer.data(), _sampleBuffer.size());
        if (bytesWritten != _sampleBuffer.size())
        {
            errMsg = "failWrite";
            rslt = false;
        }
    }

    // Close file
    fileSystem.fileClose(pFile, "", filename, true);

#ifdef DEBUG_WRITE_TO_FILE
    // Debug
    bool isFsOkEnd = fileSystem.getFileInfo("", filename, fileSizeEnd);
    LOG_I(MODULE_PREFIX, "writeToFile filename %s append %d rslt %d fileSizeStart %d isOkStart %s fileSizeEnd %d isFsOkEnd %s", 
                filename.c_str(), append, rslt, 
                fileSizeStart, isFsOkStart ? "Y" : "N",
                fileSizeEnd, isFsOkEnd ? "Y" : "N");
#endif

    // Clear buffer
    _sampleBuffer.clear();

    // Return
    return rslt;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Write to console
void SampleCollectorJSON::writeToConsole()
{
    // Write header
    LOG_I("S", "SampleCollector: %s", _sampleHeader.c_str());

    // Write lines
    uint32_t strPos = 0;
    while (strPos < _sampleBuffer.size())
    {
        // Find end of line
        uint32_t strPosEnd = strPos;
        while ((strPosEnd < _sampleBuffer.size()) && (_sampleBuffer[strPosEnd] != '\n'))
            strPosEnd++;
        // Write line
        LOG_I("S", "%.*s", strPosEnd - strPos, &_sampleBuffer[strPos]);
        // Next line
        strPos = strPosEnd + 1;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add sample
/// @param sampleJSON Sample JSON
bool SampleCollectorJSON::addSample(const String& sampleJSON)
{
#ifdef DEBUG_ADD_SAMPLE
    LOG_I(MODULE_PREFIX, "addSample enabled %s busSize %d %s", 
                _samplingEnabled ? "Y" : "N",
                _sampleBuffer.size(),
                sampleJSON.c_str());
#endif

    // Check if sampling enabled
    if (!_samplingEnabled)
        return false;

    // Allocate buffer
    if (_allocateAtStart)
    {
        _sampleBuffer.reserve(_maxTotalJSONStringSize);
        _allocateAtStart = false;
    }

    // Check if buffer will be full
    if (_sampleBuffer.size() + sampleJSON.length() + 1 >= _maxTotalJSONStringSize)
    {
        // Dump to console
        if (_dumpToConsoleWhenFull)
        {
            writeToConsole();
            _sampleBuffer.clear();
        }
        else if (_dumpToFileName.length() > 0)
        {
            // Check size of file
            uint32_t fileSize = 0;
            if (fileSystem.getFileInfo("", _dumpToFileName, fileSize) && (fileSize > _maxFileSize))
            {
                LOG_I(MODULE_PREFIX, "addSample: file %s size %d exceeds max %d", _dumpToFileName.c_str(), fileSize, _maxFileSize);
                return false;
            }

            // Write to file
            String errMsg;
            if (!writeToFile(_dumpToFileName, true, errMsg))
            {
                LOG_E(MODULE_PREFIX, "addSample: FAILED to write to file %s", errMsg.c_str());
                return false;
            }

            // Clear buffer
            _sampleBuffer.clear();
        }
        else
        {
            return false;
        }
    }

    // Check time since last sample
    uint64_t timeNowUs = micros();
    if ((_minTimeBetweenSamplesUs != 0) && (Raft::isTimeout(timeNowUs, _timeSinceLastSampleUs, _minTimeBetweenSamplesUs)))
        return false;

    // Add sample to buffer
    _sampleBuffer.insert(_sampleBuffer.end(), sampleJSON.c_str(), sampleJSON.c_str() + sampleJSON.length());
    _sampleBuffer.push_back('\n');

    // Update time since last sample
    _timeSinceLastSampleUs = timeNowUs;
    return true;
}
