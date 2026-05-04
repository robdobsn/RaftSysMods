////////////////////////////////////////////////////////////////////////////////
//
// MainSysMod.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include "MainSysMod.h"
#include "RaftUtils.h"
#include "PlatformUtils.h"

#ifdef ESP_PLATFORM
#include "esp_mac.h"
#endif

MainSysMod::MainSysMod(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
    // This code is executed when the system module is created
    // ...
}

MainSysMod::~MainSysMod()
{
    // This code is executed when the system module is destroyed
    // ...
}

void MainSysMod::setup()
{
    String configValue = config.getString("exampleGroup/exampleKey", "This Should Not Happen!");
    LOG_I(MODULE_PREFIX, "%s", configValue.c_str());

#ifdef ESP_PLATFORM
    LOG_I(MODULE_PREFIX, "WiFi STA MAC %s", getSystemMACAddressStr(ESP_MAC_WIFI_STA, ":").c_str());
    LOG_I(MODULE_PREFIX, "WiFi AP MAC %s", getSystemMACAddressStr(ESP_MAC_WIFI_SOFTAP, ":").c_str());
#endif
}

void MainSysMod::loop()
{
    // Check for loop rate
    if (Raft::isTimeout(millis(), _lastLoopMs, 1000))
    {
        // Update last loop time
        _lastLoopMs = millis();

        LOG_I(MODULE_PREFIX, "loop ESPNowTest alive");
    }
}

