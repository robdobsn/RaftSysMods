cmake_minimum_required(VERSION 3.16)

# Build REQUIRES list conditionally
set(RAFT_SYSMODS_REQUIRES RaftCore bt app_update esp_http_client esp_ringbuf)
if (NOT (DEFINED FS_TYPE AND FS_TYPE STREQUAL "littlefs"))
  list(APPEND RAFT_SYSMODS_REQUIRES spiffs)
endif()
if (DEFINED RAFT_ENABLE_SD AND RAFT_ENABLE_SD)
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
    "components/LogManager/LoggerLoki.cpp"
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
  PRIV_REQUIRES
    esp_driver_uart
    esp_driver_gpio
    esp_driver_usb_serial_jtag
)
