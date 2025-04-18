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
