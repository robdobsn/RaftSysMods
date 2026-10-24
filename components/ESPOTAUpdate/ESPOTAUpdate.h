/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Over-The-Air (OTA) Firmware Update
// Handles OTA
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include <deque>
#include "RaftUtils.h"
#include "RaftSysMod.h"
#include "FileStreamBlock.h"
#include "SpiramAwareAllocator.h"
#include "MiniHDLC.h"
#include "RaftThreading.h"
#include "esp_ota_ops.h"

class RestAPIEndpointManager;
class APISourceInfo;

class ESPOTAUpdate : public RaftSysMod
{
    class OTAUpdateFileBlock;

public:
    ESPOTAUpdate(const char* pModuleName, RaftJsonIF& sysConfig);

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new ESPOTAUpdate(pModuleName, sysConfig);
    }
    
    // Check if update in progress
    virtual bool isBusy() override final
    {
        return _otaDirectInProgress;
    }

    // Start/Data/Cancel methods
    virtual bool fileStreamStart(const char* fileName, size_t fileLen) override final;
    virtual RaftRetCode fileStreamDataBlock(FileStreamBlock& fileStreamBlock) override final;
    virtual bool fileStreamCancelEnd(bool isNormalEnd) override final;
    virtual RaftRetCode fileStreamWriteStatus() const override final;

    // Get debug string
    virtual String getDebugJSON() const override final;

protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;

private:

    // OTA direct (pushed from server)
    bool _otaDirectEnabled;
    static const int TIME_TO_WAIT_BEFORE_RESTART_MS = 1000;

    // Max time the reply to an HTTP update waits for the update to finish so that it can report the result
    static const uint32_t OTA_COMPLETION_WAIT_MAX_MS = 15000;

    // Restart pending (set on the file system worker - the start time is written before the flag)
    std::atomic<bool> _restartPending{false};
    volatile int _restartPendingStartMs = 0;

    // Direct update vars
    std::atomic<bool> _otaDirectInProgress{false};
    esp_ota_handle_t _espOTAHandle = -1;

    // Cancel (or end) requested - set on the main task and actioned on the file system worker, which drops the blocks
    // still waiting
    std::atomic<bool> _otaCancelRequested{false};

    // Semaphore on update status
    // No blocking calls are made with this held so waits are short. When statistics are updated or read a
    // limited wait is used (and the update is skipped on failure). When the OTA result is recorded or
    // reported the wait is forever so that the result cannot be lost or mis-reported
    SemaphoreHandle_t _fwUpdateStatusSemaphore = nullptr;
    static const uint32_t FW_UPDATE_STATS_MUTEX_MAX_WAIT_MS = 100;

    // Firmware update status
    struct FWUpdateStatus
    {
        uint64_t startUs = 0;
        uint64_t espOTABeginFnUs = 0;
        uint64_t totalWriteUs = 0;
        uint32_t totalBytes = 0;
        float updateRateBps = 0;
        uint16_t lastBlockSize = 0;
        uint16_t totalCRC = MiniHDLC::crcInitCCITT();
        bool lastOTAUpdateOK = false;
        String lastOTAUpdateResult = "NotStarted";
    };

    // Firmware update status
    FWUpdateStatus _otaStatus;

    // Requests (blocks, and cancels) waiting for the file system worker: a job there takes them in order until
    // there are none left. _otaRequestsMutex guards _otaRequests; _otaJobOutstanding is set while there are requests
    // or the job runs
    std::deque<OTAUpdateFileBlock*> _otaRequests;
    mutable RaftMutex _otaRequestsMutex;
    std::atomic<bool> _otaJobOutstanding{false};
    std::atomic<uint32_t> _otaPendingBytes{0};
    std::atomic<bool> _otaWriteFailed{false};
    static const uint32_t HTTP_PENDING_MAX_BYTES = 16384;
    static const uint32_t REQUEST_HEAP_MARGIN_BYTES = 16384;

    // Reply to an HTTP update deferred until the update is finished
    bool _httpReplyDeferred = false;
    uint32_t _httpReplyDeferToken = 0;
    String _httpReplyReqStr;
    String otaResultResp(const String& reqStr);

private:
    // Handle received data
    void onDataReceived(uint8_t *pDataReceived, size_t dataReceivedLen);

    // API ESP Firmware update
    RaftRetCode apiFirmwarePart(const String& req, FileStreamBlock& fileStreamBlock, const APISourceInfo& sourceInfo);
    RaftRetCode apiFirmwareMain(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    bool apiReadyToReceiveData(const APISourceInfo& sourceInfo);

    // Requests on the file system worker
    bool queueRequest(OTAUpdateFileBlock* pReqRec);
    void otaJob();
    void handleRequest(OTAUpdateFileBlock* pReqRec);

    // Functions used on the file system worker
    bool startOTAUpdate(size_t fileLen);
    bool completeOTAUpdate(bool cancelUpdate);

    class OTAUpdateFileBlock
    {
    public:
        OTAUpdateFileBlock(FileStreamBlock& fileStreamBlock) : fsb(fileStreamBlock)
        {
            if (fileStreamBlock.filename)
                fileName = fileStreamBlock.filename;
            if (fileStreamBlock.pBlock)
                blockData.assign(fileStreamBlock.pBlock, fileStreamBlock.pBlock + fileStreamBlock.blockLen);
            fsb.pBlock = blockData.data();
            fsb.filename = fileName.c_str();
        }
        OTAUpdateFileBlock(bool cancelUpdate) : fsb(cancelUpdate)
        {
        }
        FileStreamBlock fsb;
        String fileName;
        std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> blockData;
    };

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "ESPOTAUpdate";

};
