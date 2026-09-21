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

    // Max time apiFirmwareMain waits for the worker task to finish the update so that it can report the result
    static const uint32_t OTA_COMPLETION_WAIT_MAX_MS = 15000;

    // Restart pending (set on the worker task - the start time is written before the flag)
    std::atomic<bool> _restartPending{false};
    volatile int _restartPendingStartMs = 0;

    // Direct update vars
    std::atomic<bool> _otaDirectInProgress{false};
    esp_ota_handle_t _espOTAHandle = -1;

    // Cancel (or end) requested - set on the main task and actioned on the worker task
    // This ensures a cancel is not lost if it can't be queued because the worker is busy (e.g. in esp_ota_begin)
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

    // Task that operates the bus
    volatile TaskHandle_t _otaWorkerTaskHandle = nullptr;
    static const int DEFAULT_TASK_CORE = 0;
    static const int DEFAULT_TASK_PRIORITY = 5;
    static const int DEFAULT_TASK_STACK_SIZE_BYTES = 4000;

    // Queue of OTA update requests
    QueueHandle_t _otaUpdateQueue = nullptr;

private:
    // Handle received data
    void onDataReceived(uint8_t *pDataReceived, size_t dataReceivedLen);

    // API ESP Firmware update
    RaftRetCode apiFirmwarePart(const String& req, FileStreamBlock& fileStreamBlock, const APISourceInfo& sourceInfo);
    RaftRetCode apiFirmwareMain(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    bool apiReadyToReceiveData(const APISourceInfo& sourceInfo);

    // Worker task
    static void otaWorkerTaskStatic(void* pvParameters);
    void otaWorkerTask();

    // Functions used by task
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
