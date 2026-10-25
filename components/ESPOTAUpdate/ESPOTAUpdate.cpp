/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Over-The-Air (OTA) Firmware Update
// Handles OTA
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ESPOTAUpdate.h"
#include "RestAPIEndpointManager.h"
#include "Logger.h"
#include "esp_system.h"
#include "RaftArduino.h"
#include "SysManager.h"
#include "ProtocolExchange.h"
#include "FileSystemWorker.h"

// Debug
#define DEBUG_ESP_OTA_UPDATE_FIRST_AND_LAST
// #define DEBUG_ESP_OTA_UPDATE
// #define DEBUG_ESP_OTA_UPDATE_API_MAIN
// #define DEBUG_ESP_OTA_UPDATE_SEND_OK
// #define DEBUG_ESP_OTA_UPDATE_BLOCK_DETAIL
// #define DEBUG_ESP_OTA_UPDATE_NOT_READY

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ESPOTAUpdate::ESPOTAUpdate(const char* pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
    RaftMutex_init(_otaRequestsMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESPOTAUpdate::setup()
{
    // Extract info from config
    _otaDirectEnabled = configGetBool("OTADirect", true);
    LOG_I(MODULE_PREFIX, "setup otaDirect %s", _otaDirectEnabled ? "YES" : "NO");

    // Get the protocol exchange from the system manager
    SysManagerIF* pSysManager = getSysManager();
    if (pSysManager)
    {
        ProtocolExchange* pProtocolExchange = pSysManager->getProtocolExchange();
        if (pProtocolExchange)
            pProtocolExchange->setFWUpdateHandler(this);
    }

    // Create semaphore
    if (!_fwUpdateStatusSemaphore)
        _fwUpdateStatusSemaphore = xSemaphoreCreateMutex();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop (called frequently)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESPOTAUpdate::loop()
{
    // Reply to an HTTP update once it is finished
    if (_httpReplyDeferred && !_otaJobOutstanding.load())
    {
        _httpReplyDeferred = false;
        RestAPIEndpointManager* pRestAPIEndpointManager = getRestAPIEndpointManager();
        if (pRestAPIEndpointManager)
            pRestAPIEndpointManager->completeDeferred(_httpReplyDeferToken, otaResultResp(_httpReplyReqStr));
    }

    // Check if restart is pending
    if (_restartPending && 
            Raft::isTimeout(millis(), _restartPendingStartMs, TIME_TO_WAIT_BEFORE_RESTART_MS))
    {
        _restartPending = false;
        esp_restart();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String ESPOTAUpdate::getDebugJSON() const
{
    // Obtain semaphore
    if (!_fwUpdateStatusSemaphore || (xSemaphoreTake(_fwUpdateStatusSemaphore, pdMS_TO_TICKS(FW_UPDATE_STATS_MUTEX_MAX_WAIT_MS)) != pdTRUE))
        return "{}";

    // Copy status
    FWUpdateStatus otaStatus = _otaStatus;

    // Release semaphore
    xSemaphoreGive(_fwUpdateStatusSemaphore);

    // FW update timing
    uint32_t fwUpdateElapsedMs = (micros() - otaStatus.startUs) / 1000;
    if (_otaDirectInProgress)
        otaStatus.updateRateBps = (fwUpdateElapsedMs != 0) ? 1000.0*otaStatus.totalBytes/fwUpdateElapsedMs : 0;
    char tmpBuf[200];
    snprintf(tmpBuf, sizeof(tmpBuf)-1, R"({"Bps":%.1f,"stMs":%d,"bytes":%d,"wrPS":%.1f,"elapS":%.1f,"blk":%d})",
        otaStatus.updateRateBps, 
        (int)(otaStatus.espOTABeginFnUs / 1000),
        (int)otaStatus.totalBytes,
        otaStatus.totalWriteUs != 0 ? otaStatus.totalBytes / (otaStatus.totalWriteUs / 1000000.0) : 0.0,
        fwUpdateElapsedMs / 1000.0,
        (int)otaStatus.lastBlockSize);

    return tmpBuf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// REST API endpoint definition
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESPOTAUpdate::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    endpointManager.addEndpoint("espFwUpdate",
                        RestAPIEndpoint::ENDPOINT_CALLBACK, 
                        RestAPIEndpoint::ENDPOINT_POST,
                        std::bind(&ESPOTAUpdate::apiFirmwareMain, this, 
                                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        "Update ESP32 firmware", 
                        "application/json", 
                        NULL,
                        RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
                        NULL, 
                        NULL,
                        std::bind(&ESPOTAUpdate::apiFirmwarePart, this, 
                                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        std::bind(&ESPOTAUpdate::apiReadyToReceiveData, this,
                                std::placeholders::_1));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// REST API endpoint handlers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ESPOTAUpdate::apiFirmwareMain(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // This is a POST request so the body will contain the firmware and this method 
#ifdef DEBUG_ESP_OTA_UPDATE_API_MAIN
    // Debug
    LOG_I(MODULE_PREFIX, "apiESPFirmwareMain");
#endif

    // This is called (on the main task) when the whole of the firmware image has been received - which means the
    // final block has been passed to the file system worker but NOT that it has been written. The worker still has
    // to write that block and complete the update (esp_ota_end verifies the whole image which takes some time) and
    // until it has done so the status is "InProgress", which would be reported as a failure of an update which then
    // succeeds. So the reply waits until the worker has finished (it is deferred, and sent from loop()). The restart
    // which follows a successful update is made in loop() a second later, after the reply
    if (_otaJobOutstanding.load())
    {
        RestAPIEndpointManager* pRestAPIEndpointManager = getRestAPIEndpointManager();
        if (sourceInfo.canDefer && pRestAPIEndpointManager &&
                (pRestAPIEndpointManager->deferResponse(reqStr.c_str(), sourceInfo, OTA_COMPLETION_WAIT_MAX_MS) == RAFT_RESP_DEFERRED))
        {
            _httpReplyDeferred = true;
            _httpReplyDeferToken = sourceInfo.deferToken;
            _httpReplyReqStr = reqStr;
            return RAFT_RESP_DEFERRED;
        }

        // The reply can't be deferred - wait here (bounded)
        uint32_t waitStartMs = millis();
        while (_otaJobOutstanding.load())
        {
            if (Raft::isTimeout(millis(), waitStartMs, OTA_COMPLETION_WAIT_MAX_MS))
            {
                LOG_W(MODULE_PREFIX, "apiFirmwareMain timed-out waiting for OTA update to complete");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    respStr = otaResultResp(reqStr);
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Result of the last update (reply to the HTTP update)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String ESPOTAUpdate::otaResultResp(const String& reqStr)
{
    // Get status (wait forever - see note in header - so a busy mutex cannot result in failure being reported)
    FWUpdateStatus otaStatus;
    if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, portMAX_DELAY) == pdTRUE))
    {
        // Copy status
        otaStatus = _otaStatus;

        // Release semaphore
        xSemaphoreGive(_fwUpdateStatusSemaphore);
    }
    String respStr;
    Raft::setJsonResult(reqStr.c_str(), respStr, otaStatus.lastOTAUpdateOK, otaStatus.lastOTAUpdateResult.c_str());
    return respStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ESP Firmware update
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ESPOTAUpdate::apiFirmwarePart(const String& reqStr, FileStreamBlock& fileStreamBlock, const APISourceInfo& sourceInfo)
{
    // Handle with OTA update
#ifdef DEBUG_ESP_OTA_UPDATE
    // Debug
    LOG_I(MODULE_PREFIX, "apiESPFirmwarePart %d %d %d %s %s", 
                fileStreamBlock.contentLen, fileStreamBlock.filePos, 
                fileStreamBlock.blockLen, 
                fileStreamBlock.firstBlock ? "Y" : "N",
                fileStreamBlock.finalBlock ? "Y" : "N");
#endif

    // Handle block
    return fileStreamDataBlock(fileStreamBlock);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ready to receive data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESPOTAUpdate::apiReadyToReceiveData(const APISourceInfo& sourceInfo)
{
    // Not ready while the blocks waiting to be written are at their limit (the web server then stops reading the
    // request so the client is slowed by TCP flow control)
    bool isReady = _otaPendingBytes.load() < HTTP_PENDING_MAX_BYTES;
#ifdef DEBUG_ESP_OTA_UPDATE_NOT_READY
    if (!isReady)
        LOG_I(MODULE_PREFIX, "apiRdy not ready pending %d", (int)_otaPendingBytes.load());
#endif
    return isReady;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle the start of a file stream operation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESPOTAUpdate::fileStreamStart(const char* fileName, size_t fileLen)
{
    // Check enabled
    if (!_otaDirectEnabled)
    {
        LOG_W(MODULE_PREFIX, "fileStreamStart OTA Direct Disabled");
        return false;
    }

#ifdef DEBUG_ESP_OTA_UPDATE
    // Debug
    LOG_I(MODULE_PREFIX, "fileStreamStart %s %d", fileName, fileLen);
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle a file stream block (from API or other source) - it is written on the file system worker
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ESPOTAUpdate::fileStreamDataBlock(FileStreamBlock& fileStreamBlock)
{
    // Check if this is the start of a new update
    if (!_otaDirectInProgress && fileStreamBlock.firstBlock)
    {
        if (!fileStreamStart(fileStreamBlock.filename, 
                    fileStreamBlock.fileLenValid ? fileStreamBlock.fileLen : fileStreamBlock.contentLen))
            return RAFT_INVALID_OPERATION;
        _otaWriteFailed = false;
    }

    // A block that failed to write stops the update
    if (_otaWriteFailed.load())
        return RAFT_OTHER_FAILURE;

    // Check the memory is there for a copy of the block (with exceptions disabled a failed allocation would crash)
    if (fileStreamBlock.blockLen + REQUEST_HEAP_MARGIN_BYTES > SpiramAwareAllocator<uint8_t>::max_allocatable())
    {
        LOG_E(MODULE_PREFIX, "fileStreamDataBlock no memory for block len %d", (int)fileStreamBlock.blockLen);
        return RAFT_INSUFFICIENT_RESOURCE;
    }

    // Queue a copy of the block
    if (!queueRequest(new OTAUpdateFileBlock(fileStreamBlock)))
        return RAFT_OTHER_FAILURE;
#ifdef DEBUG_ESP_OTA_UPDATE_SEND_OK
    LOG_I(MODULE_PREFIX, "fileStreamDataBlock queued len %d", (int)fileStreamBlock.blockLen);
#endif
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Firmware update cancel
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESPOTAUpdate::fileStreamCancelEnd(bool isNormalEnd)
{
    // Flag that cancel (or end) is requested - the job checks the flag after each request and then drops the blocks
    // still waiting. Note that the job ignores the request if an update is not in progress (which is the case after
    // a normal end) so the result of a completed update is not overwritten
    _otaCancelRequested = true;

    // Queue a cancel request (this starts the job if it isn't running)
    queueRequest(new OTAUpdateFileBlock(true));
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status of blocks written in the background (the file upload acknowledges blocks once they are written)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ESPOTAUpdate::fileStreamWriteStatus() const
{
    if (_otaJobOutstanding.load())
        return RAFT_BUSY;
    return _otaWriteFailed.load() ? RAFT_OTHER_FAILURE : RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queue a request and start the job on the file system worker if it isn't running
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESPOTAUpdate::queueRequest(OTAUpdateFileBlock* pReqRec)
{
    if (!pReqRec)
        return false;
    RaftMutex_lock(_otaRequestsMutex, RAFT_MUTEX_WAIT_FOREVER);
    _otaRequests.push_back(pReqRec);
    _otaPendingBytes += pReqRec->fsb.blockLen;
    bool startJob = !_otaJobOutstanding.load();
    _otaJobOutstanding = true;
    RaftMutex_unlock(_otaRequestsMutex);

    // Start the job (or handle the requests here if the worker can't take it)
    if (startJob && !fileSystemWorker.submit(std::bind(&ESPOTAUpdate::otaJob, this)))
        otaJob();
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Job on the file system worker: handles the requests in order until there are none left
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESPOTAUpdate::otaJob()
{
    while (true)
    {
        // Take the next request
        RaftMutex_lock(_otaRequestsMutex, RAFT_MUTEX_WAIT_FOREVER);
        if (_otaRequests.empty())
        {
            _otaJobOutstanding = false;
            RaftMutex_unlock(_otaRequestsMutex);
            return;
        }
        OTAUpdateFileBlock* pReqRec = _otaRequests.front();
        _otaRequests.pop_front();
        RaftMutex_unlock(_otaRequestsMutex);

        // Handle it
        handleRequest(pReqRec);
        _otaPendingBytes -= pReqRec->fsb.blockLen;
        delete pReqRec;

        // Cancel requested: the update is ended and the blocks still waiting are dropped
        if (_otaCancelRequested.exchange(false))
        {
            if (_otaDirectInProgress)
            {
                LOG_I(MODULE_PREFIX, "otaJob cancel update");
                completeOTAUpdate(true);
            }
            RaftMutex_lock(_otaRequestsMutex, RAFT_MUTEX_WAIT_FOREVER);
            for (OTAUpdateFileBlock* pDropped : _otaRequests)
            {
                _otaPendingBytes -= pDropped->fsb.blockLen;
                delete pDropped;
            }
            _otaRequests.clear();
            RaftMutex_unlock(_otaRequestsMutex);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle a request (a block or a cancel) on the file system worker
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESPOTAUpdate::handleRequest(OTAUpdateFileBlock* pReqRec)
{
    // Handle the request
    if (pReqRec->fsb.isCancelUpdate())
    {
        // Cancel update (if in progress - a cancel is also requested after a normal end and,
        // in that case, the result of the completed update must not be overwritten)
        if (_otaDirectInProgress)
        {
            LOG_I(MODULE_PREFIX, "otaJob cancel update");
            completeOTAUpdate(true);
        }
    }
    else
    {

        // Handle block
#ifdef DEBUG_ESP_OTA_UPDATE_FIRST_AND_LAST
        // Debug
        if (pReqRec->fsb.firstBlock || pReqRec->fsb.finalBlock)
        {
            LOG_I(MODULE_PREFIX, "otaJob blkLen %d pos %d bytesRcvd %d %s", 
                    pReqRec->fsb.blockLen, pReqRec->fsb.filePos,
                    pReqRec->fsb.filePos + pReqRec->fsb.blockLen,
                    pReqRec->fsb.firstBlock ? "FIRST" : pReqRec->fsb.finalBlock ? "FINAL" : "");
        }
#endif
#ifdef DEBUG_ESP_OTA_UPDATE_BLOCK_DETAIL
        LOG_I(MODULE_PREFIX, "otaJob blkLen %d pos %d %s", 
                    pReqRec->fsb.blockLen, pReqRec->fsb.filePos,
                    pReqRec->fsb.firstBlock ? "FIRST" : pReqRec->fsb.finalBlock ? "FINAL" : "");
        if (pReqRec->fsb.firstBlock || pReqRec->fsb.finalBlock)
        {
            String outStr;
            Raft::hexDump(pReqRec->fsb.pBlock, pReqRec->fsb.blockLen, outStr);
            LOG_I(MODULE_PREFIX, "otaJob %s bytesRcvd %d data %s", 
                        pReqRec->fsb.firstBlock ? "FIRST" : pReqRec->fsb.finalBlock ? "FINAL" : "",
                        pReqRec->fsb.filePos + pReqRec->fsb.blockLen,
                        outStr.c_str());
        }
#endif

        // Check for start
        bool isOk = true;
        String failReason;
        if (pReqRec->fsb.firstBlock)
        {
            isOk = startOTAUpdate(pReqRec->fsb.fileLenValid ? pReqRec->fsb.fileLen : pReqRec->fsb.contentLen);
            if (!isOk)
            {
                failReason = "FailedStartOTA";
            }
        }

        // Check if update in progress
        const uint8_t* pBlock = pReqRec->fsb.pBlock;
        uint32_t blockLen = pReqRec->fsb.blockLen;
        if (isOk && _otaDirectInProgress && pBlock && (blockLen > 0))
        {
            // Write block
            uint64_t fwStart = micros();
            esp_err_t err = esp_ota_write(_espOTAHandle, (const void *)pBlock, blockLen);

            // Check result
            if (err == ESP_OK) 
            {
                // Calculate CRC outside the mutex (totalCRC is only written on this task so can be read here)
                uint64_t writeUs = micros() - fwStart;
                uint16_t totalCRC = MiniHDLC::crcUpdateCCITT(_otaStatus.totalCRC, pBlock, blockLen);

                // Update status (statistics only so failure to obtain the mutex is not a failure of the update)
                if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, pdMS_TO_TICKS(FW_UPDATE_STATS_MUTEX_MAX_WAIT_MS)) == pdTRUE))
                {
                    _otaStatus.totalWriteUs += writeUs;
                    _otaStatus.totalBytes += blockLen;
                    _otaStatus.lastBlockSize = blockLen;
                    _otaStatus.totalCRC = totalCRC;
                    xSemaphoreGive(_fwUpdateStatusSemaphore);
                }
            }
            else
            {
                // Failed
                isOk = false;
                failReason = "FailedWriteOTA";
                LOG_E(MODULE_PREFIX, "otaJob esp_ota_write FAILED err=0x%x", err);
            }
        }

        // Check if final
        if (isOk && pReqRec->fsb.finalBlock)
        {
            completeOTAUpdate(false);
        }

        // Handle failures
        if (!isOk)
        {
            _otaWriteFailed = true;
            // No longer in progress (abort the update to release the OTA handle if it was started)
            if (_otaDirectInProgress.exchange(false))
                esp_ota_abort(_espOTAHandle);

            // Update status
            if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, portMAX_DELAY) == pdTRUE))
            {
                _otaStatus.lastOTAUpdateOK = false;
                _otaStatus.lastOTAUpdateResult = "Failed";
                xSemaphoreGive(_fwUpdateStatusSemaphore);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start OTA update
bool ESPOTAUpdate::startOTAUpdate(size_t fileLen)
{
    // Abort any update already in progress (to release the OTA handle)
    if (_otaDirectInProgress.exchange(false))
        esp_ota_abort(_espOTAHandle);

    // Obtain semaphore (wait forever - see note in header - the update is not failed because of the status mutex)
    if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, portMAX_DELAY) == pdTRUE))
    {
        // Timing
        _otaStatus.startUs = micros();
        _otaStatus.espOTABeginFnUs = 0;
        _otaStatus.totalWriteUs = 0;
        _otaStatus.totalBytes = 0;
        _otaStatus.lastBlockSize = 0;
        _otaStatus.totalCRC = MiniHDLC::crcInitCCITT();
        _otaStatus.lastOTAUpdateOK = false;
        _otaStatus.lastOTAUpdateResult = "InProgress";

        // Release semaphore
        xSemaphoreGive(_fwUpdateStatusSemaphore);
    }

    // Get update partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition)
    {
        LOG_E(MODULE_PREFIX, "startOTAUpdate esp_ota_get_next_update_partition failed");
        return false;
    }

#ifdef DEBUG_ESP_OTA_UPDATE
    // Debug
    LOG_I(MODULE_PREFIX, "startOTAUpdate fileLen %d", fileLen);
#endif

    // const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
    {
        LOG_E(MODULE_PREFIX, "startOTAUpdate esp_ota_get_running_partition failed");
        return false;
    }

    // Info
    LOG_I(MODULE_PREFIX, "startOTAUpdate running partition type %d subtype %d (offset 0x%x)",
                running->type, running->subtype, running->address);

    LOG_I(MODULE_PREFIX, "startOTAUpdate writing to partition subtype %d at offset 0x%x",
            update_partition->subtype, update_partition->address);

    // Determine the size to pass to esp_ota_begin. Passing the actual image size
    // (rather than OTA_SIZE_UNKNOWN) means esp_ota_begin erases only the sectors
    // required for the image instead of the entire partition - this significantly
    // reduces the up-front flash-erase time (and hence the period during which the
    // flash cache is disabled and the web server task is starved).
    size_t eraseSize = OTA_SIZE_UNKNOWN;
    if ((fileLen > 0) && (fileLen <= update_partition->size))
        eraseSize = fileLen;

    // Start OTA update.
    // esp_ota_begin erases up to `eraseSize` bytes and disables the flash cache
    // for the duration, which can take several seconds. While the cache is off,
    // the IDLE task on the current core cannot run, so the task watchdog may
    // trigger and print a backtrace. The OTA itself is unaffected (the backtrace
    // is informational) and the longer idle timeout in RaftWebConnection now
    // tolerates the stall, so we leave the WDT alone here.
    uint64_t otaBeginStartUs = micros();
    esp_err_t err = esp_ota_begin(update_partition, eraseSize, &_espOTAHandle);
    uint64_t otaBeginElapsedUs = micros() - otaBeginStartUs;

    // Timeing of esp_ota_begin
    if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, pdMS_TO_TICKS(FW_UPDATE_STATS_MUTEX_MAX_WAIT_MS)) == pdTRUE))
    {
        _otaStatus.espOTABeginFnUs = micros() - _otaStatus.startUs;
        xSemaphoreGive(_fwUpdateStatusSemaphore);
    }

    // Info on erase time as this blocks the system (flash cache disabled) and is the
    // main cause of long stalls during OTA - useful for diagnosing connection timeouts
    LOG_I(MODULE_PREFIX, "startOTAUpdate esp_ota_begin eraseSize %d (fileLen %d partSize %d) took %.1f ms result %s",
            (int)eraseSize, (int)fileLen, (int)update_partition->size,
            otaBeginElapsedUs / 1000.0, esp_err_to_name(err));

    // Check result
    if (err == ESP_OK) 
    {
        _otaDirectInProgress = true;
#ifdef DEBUG_ESP_OTA_UPDATE
        // Debug
        LOG_I(MODULE_PREFIX, "startOTAUpdate esp_ota_begin succeeded");
#endif
    }
    else
    {
#ifdef DEBUG_ESP_OTA_UPDATE
        // Debug
        LOG_E(MODULE_PREFIX, "startOTAUpdate esp_ota_begin failed, error=%d", err);
        return false;
#endif
    }
    return err == ESP_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Finalize firmware update (and reboot)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESPOTAUpdate::completeOTAUpdate(bool updateCancelled)
{
    // Finish OTA
    bool wasInProgress = _otaDirectInProgress.exchange(false);

    // Check if cancelled
    if (updateCancelled)
    {
        LOG_I(MODULE_PREFIX, "completeOTAUpdate cancelled");

        // Abort the update to release the OTA handle
        if (wasInProgress)
            esp_ota_abort(_espOTAHandle);
        if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, portMAX_DELAY) == pdTRUE))
        {
            _otaStatus.lastOTAUpdateOK = false;
            _otaStatus.lastOTAUpdateResult = "FailedCancelled";
            xSemaphoreGive(_fwUpdateStatusSemaphore);
        }
        return false;
    }

    // Debug
    if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, pdMS_TO_TICKS(FW_UPDATE_STATS_MUTEX_MAX_WAIT_MS)) == pdTRUE))
    {
        FWUpdateStatus otaStatus = _otaStatus;
        xSemaphoreGive(_fwUpdateStatusSemaphore);

        // Completing OTA update
        LOG_I(MODULE_PREFIX, "completeOTAUpdate completing total bytes received %d CRC %04x", otaStatus.totalBytes, otaStatus.totalCRC);
    }

    // Check if update is valid
    if (esp_ota_end(_espOTAHandle) != ESP_OK) 
    {
        LOG_E(MODULE_PREFIX, "esp_ota_end failed!");
        if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, portMAX_DELAY) == pdTRUE))
        {
            _otaStatus.lastOTAUpdateOK = false;
            _otaStatus.lastOTAUpdateResult = "FailedOTAEnd";
            xSemaphoreGive(_fwUpdateStatusSemaphore);
        }
        return false;
    }

    // Get update partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);

    // Set boot partition
    esp_err_t err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) 
    {
        LOG_E(MODULE_PREFIX, "esp_ota_set_boot_partition failed! err=0x%x", err);
        if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, portMAX_DELAY) == pdTRUE))
        {
            _otaStatus.lastOTAUpdateOK = false;
            _otaStatus.lastOTAUpdateResult = "FailedSetBootPartition";
            xSemaphoreGive(_fwUpdateStatusSemaphore);
        }
        return false;
    }

#ifdef DEBUG_ESP_OTA_UPDATE
    // Debug
    LOG_I(MODULE_PREFIX, "esp_ota_set_boot_partition ok ... reboot pending");
#endif

    // Schedule restart
    _restartPendingStartMs = millis();
    _restartPending = true;
    if (_fwUpdateStatusSemaphore && (xSemaphoreTake(_fwUpdateStatusSemaphore, portMAX_DELAY) == pdTRUE))
        {
            _otaStatus.lastOTAUpdateOK = true;
            _otaStatus.lastOTAUpdateResult = "OK";
            xSemaphoreGive(_fwUpdateStatusSemaphore);
        }
    return true;
}
