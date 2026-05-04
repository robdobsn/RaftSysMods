# Include common features
include("${BUILD_CONFIG_DIR}/../Common/features.cmake")

set(DEV_TYPE_JSON_FILES "/devtypes/DeviceTypeRecords.json")

# Build the optional ESPNow transport SysMod into this test app.
set(RAFT_SYSMODS_ENABLE_ESPNOW ON)

