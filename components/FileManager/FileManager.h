/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileManager 
// Handles SPIFFS/LittleFS and SD card file access
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "RaftThreading.h"
#include "RaftUtils.h"
#include "FileStreamBlock.h"
#include "ProtocolExchange.h"
#include <list>
#include <memory>
#include <atomic>

class RestAPIEndpointManager;
class APISourceInfo;

class FileManager : public RaftSysMod
{
public:
    FileManager(const char* pModuleName, RaftJsonIF& sysConfig);

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new FileManager(pModuleName, sysConfig);
    }
    
    // // Upload file to file system
    // virtual bool fileStreamDataBlock(FileStreamBlock& fileStreamBlock) override final;

protected:
    // Setup
    virtual void setup() override final;

    // Loop - called frequently
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;
private:

    // Protocol exchange
    ProtocolExchange* _pProtocolExchange;

    // Requests answered once work on the file system worker is finished (the reply is deferred): a file hash, or
    // the end of an HTTP upload (replied once the upload is written)
    struct DeferredFileJob
    {
        bool isUploadEnd = false;
        uint32_t deferToken = 0;
        uint32_t channelID = 0;
        String reqStr;
        String respStr;
        std::atomic<bool> done{false};
    };
    std::list<std::shared_ptr<DeferredFileJob>> _deferredFileJobs;
    static const uint32_t FILE_HASH_TIMEOUT_MS = 60000;
    static const uint32_t UPLOAD_END_TIMEOUT_MS = 60000;
    void serviceDeferredFileJobs();

    // File hash (CRC16-CCITT) reply
    static String fileHashResp(const String& reqStr, const String& fileSystemStr, const String& fileNameStr,
                uint32_t fileLength);

    // Helpers
    void applySetup();

    // Format file system
    RaftRetCode apiReformatFS(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // List files on a file system
    // In the reqStr the first part of the path is the file system name (e.g. sd or local, can be blank to default)
    // The second part of the path is the folder - note that / must be replaced with ~ in folder
    RaftRetCode apiFileList(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Read file contents
    // In the reqStr the first part of the path is the file system name (e.g. sd or local)
    // The second part of the path is the folder and filename - note that / must be replaced with ~ in folder
    RaftRetCode apiFileRead(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Read a binary-safe section of a file (base64 encoded in a JSON response)
    // reqStr: filesection/<fs>/<filename>/<start>/<len>  (/ replaced with ~ in filename)
    // Response: {"rslt":"ok","fs":..,"fname":..,"start":..,"len":..,"total":..,"data":"<base64>"}
    RaftRetCode apiFileSection(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Compute a content hash (CRC16-CCITT) of a file - used for sync change detection
    // reqStr: filehash/<fs>/<filename>  (/ replaced with ~ in filename)
    // Response: {"rslt":"ok","fs":..,"fname":..,"size":..,"crc16":"<hex>"}
    RaftRetCode apiFileHash(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Delete file on the file system
    // In the reqStr the first part of the path is the file system name (e.g. sd or local)
    // The second part of the path is the filename - note that / must be replaced with ~ in filename
    RaftRetCode apiDeleteFile(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // API upload file to file system - completed
    RaftRetCode apiUploadFileComplete(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);

    // Upload file to file system - part of file (from HTTP POST file)
    RaftRetCode apiUploadFileBlock(const String& req, FileStreamBlock& fileStreamBlock, const APISourceInfo& sourceInfo);

    // Log prefix
    static constexpr const char *MODULE_PREFIX = "FileMan";

};
