cmake_minimum_required(VERSION 3.16)

# Build REQUIRES list conditionally
set(RAFT_SYSMODS_REQUIRES RaftCore bt app_update esp_http_client esp_ringbuf)
set(RAFT_SYSMODS_ESPNOW_SELECTED OFF)
if ((DEFINED RAFT_SYSMODS_ENABLE_ESPNOW AND RAFT_SYSMODS_ENABLE_ESPNOW) OR CONFIG_RAFT_SYSMODS_ENABLE_ESPNOW)
  set(RAFT_SYSMODS_ESPNOW_SELECTED ON)
endif()
set(RAFT_SYSMODS_SRCS
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
)
set(RAFT_SYSMODS_INCLUDE_DIRS
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
)
if (NOT (DEFINED FS_TYPE AND FS_TYPE STREQUAL "littlefs"))
  list(APPEND RAFT_SYSMODS_REQUIRES spiffs)
endif()
if (DEFINED RAFT_ENABLE_SD AND RAFT_ENABLE_SD)
  list(APPEND RAFT_SYSMODS_REQUIRES fatfs)
endif()
if (RAFT_SYSMODS_ESPNOW_SELECTED)
  list(APPEND RAFT_SYSMODS_REQUIRES esp_wifi)
  list(APPEND RAFT_SYSMODS_SRCS
    "components/ESPNowManager/ESPNowConfig.cpp"
    "components/ESPNowManager/ESPNowManager.cpp"
  )
  list(APPEND RAFT_SYSMODS_INCLUDE_DIRS
    "components/ESPNowManager"
  )
endif()

idf_component_register(
  NAME
    "RaftSysMods"
  SRCS
    ${RAFT_SYSMODS_SRCS}
  INCLUDE_DIRS
    ${RAFT_SYSMODS_INCLUDE_DIRS}
  REQUIRES
    ${RAFT_SYSMODS_REQUIRES}
  PRIV_REQUIRES
    esp_driver_uart
    esp_driver_gpio
    esp_driver_usb_serial_jtag
)

if (RAFT_SYSMODS_ESPNOW_SELECTED)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC RAFT_SYSMODS_ENABLE_ESPNOW=1)
  idf_build_set_property(COMPILE_DEFINITIONS "RAFT_SYSMODS_ENABLE_ESPNOW=1" APPEND)
endif()
