/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Over-The-Air (OTA) Firmware Update
// Handles OTA
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftUtils.h"
#include "SysModBase.h"
#include "FileStreamBlock.h"
#include "SpiramAwareAllocator.h"
#include "MiniHDLC.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class RestAPIEndpointManager;
class APISourceInfo;

class ESPOTAUpdate : public SysModBase
{
public:
    ESPOTAUpdate(const char* pModuleName, RaftJsonIF& sysConfig);

    // Create function (for use by SysManager factory)
    static SysModBase* create(const char* pModuleName, RaftJsonIF& sysConfig)
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
    virtual String getDebugJSON() override final;

protected:
    // Setup
    virtual void setup() override final;

    // Service - called frequently
    virtual void service() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;

private:

    // OTA direct (pushed from server)
    bool _otaDirectEnabled;
    static const int TIME_TO_WAIT_BEFORE_RESTART_MS = 1000;

    // Restart pending
    volatile bool _restartPending = false;
    volatile int _restartPendingStartMs = 0;

    // Direct update vars
    volatile bool _otaDirectInProgress = false;
    esp_ota_handle_t _espOTAHandle = -1;

    // Semaphore on update status
    SemaphoreHandle_t _fwUpdateStatusSemaphore = nullptr;

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
};
