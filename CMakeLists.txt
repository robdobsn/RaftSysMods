cmake_minimum_required(VERSION 3.16)

# Build REQUIRES list conditionally
set(RAFT_SYSMODS_REQUIRES RaftCore bt app_update)
if (NOT DEFINED RAFT_FILESYSTEM_SPIFFS_DISABLED)
  list(APPEND RAFT_SYSMODS_REQUIRES spiffs)
endif()
if (NOT DEFINED RAFT_FILESYSTEM_FATSD_DISABLED)
  list(APPEND RAFT_SYSMODS_REQUIRES fatfs)
endif()

idf_component_register(
  NAME
    "RaftSysMods"
  SRCS
    "components/BLEManager/BLEAdvertDecoder.cpp"
    "components/BLEManager/BLEBusDeviceManager.cpp"
    "components/BLEManager/BLEGapServer.cpp"
    "components/BLEManager/BLEGattOutbound.cpp"
    "components/BLEManager/BLEGattServer.cpp"
    "components/BLEManager/BLEManager.cpp"
    "components/BLEManager/BLEStdServices.cpp"
    "components/BLEManager/BusBLE.cpp"
    "components/CommandFile/CommandFile.cpp"
    "components/CommandSerial/CommandSerial.cpp"
    "components/CommandSerial/CommandSerialPort.cpp"
    "components/CommandSocket/CommandSocket.cpp"
    "components/DataCollection/SampleCollectorJSON.cpp"
    "components/ESPOTAUpdate/ESPOTAUpdate.cpp"
    "components/FileManager/FileManager.cpp"
    "components/LogManager/LoggerPapertrail.cpp"
    "components/LogManager/LoggerRaftRemote.cpp"
    "components/LogManager/LogManager.cpp"
    "components/MQTTManager/MQTTManager.cpp"
    "components/NetworkManager/NetworkManager.cpp"
    "components/SerialConsole/SerialConsole.cpp"
    "components/StatePublisher/StatePublisher.cpp"
  INCLUDE_DIRS
    "components/BLEManager"
    "components/CommandFile"
    "components/CommandSerial"
    "components/CommandSocket"
    "components/DataCollection"
    "components/ESPOTAUpdate"
    "components/FileManager"
    "components/LogManager"
    "components/MQTTManager"
    "components/NetworkManager"
    "components/RegisterSysMods"
    "components/SerialConsole"
    "components/StatePublisher"
  REQUIRES
    ${RAFT_SYSMODS_REQUIRES}
)
