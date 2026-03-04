/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FlashCriticalSection
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "RaftThreading.h"
#include "FlashCriticalSectionIF.h"

class FlashCriticalSection : public RaftSysMod, public FlashCriticalSectionIF
{
public:
    FlashCriticalSection(const char* pModuleName, RaftJsonIF& sysConfig);
    ~FlashCriticalSection() override;

    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new FlashCriticalSection(pModuleName, sysConfig);
    }

    void enterFlashCritical(const char* reason) override;
    void exitFlashCritical(const char* reason) override;
    void registerListener(FlashCriticalListener* listener) override;
    void unregisterListener(FlashCriticalListener* listener) override;

    String getDebugJSON() const override final;

protected:
    void setup() override final;

private:
    void notifyEnter(const char* reason);
    void notifyExit(const char* reason);

    mutable RaftMutex _accessMutex;
    std::vector<FlashCriticalListener*> _listeners;
    int _refCount = 0;
    String _lastReason;

    static constexpr const char* MODULE_PREFIX = "FlashCrit";
};
