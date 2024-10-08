/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileManager 
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "FileManager.h"
#include "FileSystem.h"
#include "ConfigPinMap.h"
#include "RestAPIEndpointManager.h"
#include "Logger.h"
#include "SysManager.h"

// #define DEBUG_FILE_MANAGER_FILE_LIST
// #define DEBUG_FILE_MANAGER_FILE_LIST_DETAIL
// #define DEBUG_FILE_MANAGER_UPLOAD

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileManager::FileManager(const char *pModuleName, RaftJsonIF& sysConfig) 
        : RaftSysMod(pModuleName, sysConfig)
{
    _pProtocolExchange = nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileManager::setup()
{
    // Apply setup
    applySetup();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Apply Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileManager::applySetup()
{
    // Config settings
    String localFsDefaultName = configGetString("LocalFsDefault", "");
    FileSystem::LocalFileSystemType localFsTypeDefault = FileSystem::LOCAL_FS_DISABLE;
    if (localFsDefaultName.equalsIgnoreCase("spiffs"))
        localFsTypeDefault = FileSystem::LOCAL_FS_SPIFFS;
    else if (localFsDefaultName.equalsIgnoreCase("littlefs"))
        localFsTypeDefault = FileSystem::LOCAL_FS_LITTLEFS;
    bool localFsFormatIfCorrupt = configGetBool("LocalFsFormatIfCorrupt", false);
    bool enableSD = configGetBool("SDEnabled", false);
    bool defaultToSDIfAvailable = configGetBool("DefaultSD", false);
    bool cacheFileSystemInfo = configGetBool("CacheFileSysInfo", false);

    // SD pins
    String pinName = configGetString("SDMOSI", "");
    int sdMOSIPin = ConfigPinMap::getPinFromName(pinName.c_str());
    pinName = configGetString("SDMISO", "");
    int sdMISOPin = ConfigPinMap::getPinFromName(pinName.c_str());
    pinName = configGetString("SDCLK", "");
    int sdCLKPin = ConfigPinMap::getPinFromName(pinName.c_str());
    pinName = configGetString("SDCS", "");
    int sdCSPin = ConfigPinMap::getPinFromName(pinName.c_str());

    // Get the Protocol Exchange from the SysManager
    SysManager* pSysMan = getSysManager();
    if (pSysMan)
        _pProtocolExchange = pSysMan->getProtocolExchange();

    // Setup file system
    fileSystem.setup(localFsTypeDefault, localFsFormatIfCorrupt, enableSD, sdMOSIPin, sdMISOPin, sdCLKPin, sdCSPin, 
                defaultToSDIfAvailable, cacheFileSystemInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileManager::loop()
{
    // Service the file system
    fileSystem.loop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// REST API Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileManager::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    endpointManager.addEndpoint("reformatfs", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                    std::bind(&FileManager::apiReformatFS, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                    "Reformat file system e.g. /local or /local/force");
    endpointManager.addEndpoint("filelist", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                    std::bind(&FileManager::apiFileList, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                    "List files in folder e.g. /local/folder ... ~ for / in folder");
    endpointManager.addEndpoint("fileread", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                    std::bind(&FileManager::apiFileRead, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                    "Read file ... name", "text/plain");
    endpointManager.addEndpoint("filedelete", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                    std::bind(&FileManager::apiDeleteFile, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                    "Delete file e.g. /local/filename ... ~ for / in filename");
    endpointManager.addEndpoint("fileupload", 
                    RestAPIEndpoint::ENDPOINT_CALLBACK, 
                    RestAPIEndpoint::ENDPOINT_POST,
                    std::bind(&FileManager::apiUploadFileComplete, this, 
                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                    "Upload file", 
                    "application/json", 
                    nullptr,
                    RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
                    nullptr, 
                    nullptr,
                    std::bind(&FileManager::apiUploadFileBlock, this, 
                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format file system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileManager::apiReformatFS(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // File system
    String fileSystemStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    String forceFormat = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
    bool restartRequired = fileSystem.reformat(fileSystemStr, respStr, forceFormat.equalsIgnoreCase("force"));
    if (restartRequired)
    {
        // Restart required
        SysManager* pSysMan = getSysManager();
        if (pSysMan)
            pSysMan->systemRestart();
    }
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// List files on a file system
// In the reqStr the first part of the path is the file system name (e.g. sd or local, can be blank to default)
// The second part of the path is the folder - note that / must be replaced with ~ in folder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileManager::apiFileList(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // File system
    String fileSystemStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    // Folder
    String folderStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
    String extraPath = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 3);
    if (extraPath.length() > 0)
        folderStr += "/" + extraPath;

#ifdef DEBUG_FILE_MANAGER_FILE_LIST
    LOG_I(MODULE_PREFIX, "apiFileList req %s fileSysStr %s folderStr %s", reqStr.c_str(), fileSystemStr.c_str(), folderStr.c_str());
#endif

    folderStr.replace("~", "/");
    if (folderStr.length() == 0)
        folderStr = "/";
    fileSystem.getFilesJSON(reqStr.c_str(), fileSystemStr, folderStr, respStr);

#ifdef DEBUG_FILE_MANAGER_FILE_LIST_DETAIL
    LOG_W(MODULE_PREFIX, "apiFileList respStr %s", respStr.c_str());
#endif
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read file contents
// In the reqStr the first part of the path is the file system name (e.g. sd or local)
// The second part of the path is the folder and filename - note that / must be replaced with ~ in folder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileManager::apiFileRead(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // File system
    String fileSystemStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    // Filename
    String fileNameStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
    String extraPath = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 3);
    if (extraPath.length() > 0)
        fileNameStr += "/" + extraPath;
    fileNameStr.replace("~", "/");
    char* pFileContents = (char*)fileSystem.getFileContents(fileSystemStr, fileNameStr);
    if (!pFileContents)
    {
        respStr = "";
        return RaftRetCode::RAFT_CANNOT_START;
    }
    respStr = pFileContents;
    free(pFileContents);
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Delete file on the file system
// In the reqStr the first part of the path is the file system name (e.g. sd or local)
// The second part of the path is the filename - note that / must be replaced with ~ in filename
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileManager::apiDeleteFile(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // File system
    String fileSystemStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    // Filename
    String filenameStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
    String extraPath = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 3);
    if (extraPath.length() > 0)
        filenameStr += "/" + extraPath;
    bool rslt = false;
    filenameStr.replace("~", "/");
    if (filenameStr.length() != 0)
        rslt = fileSystem.deleteFile(fileSystemStr, filenameStr);
    LOG_I(MODULE_PREFIX, "deleteFile reqStr %s fs %s, filename %s rslt %s", 
                        reqStr.c_str(), fileSystemStr.c_str(), filenameStr.c_str(),
                        rslt ? "ok" : "fail");
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Upload file to file system - completed
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileManager::apiUploadFileComplete(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
#ifdef DEBUG_FILE_MANAGER_UPLOAD
    LOG_I(MODULE_PREFIX, "uploadFileComplete %s", reqStr.c_str());
#endif
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Upload file to file system - part of file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileManager::apiUploadFileBlock(const String& req, FileStreamBlock& fileStreamBlock, const APISourceInfo& sourceInfo)
{
    if (_pProtocolExchange)
        return _pProtocolExchange->handleFileUploadBlock(req, fileStreamBlock, sourceInfo, 
                    FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE, "fileupload");
    return RAFT_INVALID_OPERATION;
}

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // Upload file to file system - part of file (from HTTP POST file)
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// bool FileManager::fileStreamDataBlock(FileStreamBlock& fileStreamBlock)
// {
// #ifdef DEBUG_FILE_MANAGER_UPLOAD
//     LOG_I(MODULE_PREFIX, "fileStreamDataBlock fileName %s fileLen %d filePos %d blockLen %d isFinal %d", 
//             fileStreamBlock.filename ? fileStreamBlock.filename : "<null>", 
//             fileStreamBlock.fileLen, fileStreamBlock.filePos,
//             fileStreamBlock.blockLen, fileStreamBlock.finalBlock);
// #endif

//     // Handle block
//     return _fileTransferHelper.handleRxBlock(fileStreamBlock);
// }

