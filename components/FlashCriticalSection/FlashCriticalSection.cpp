/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FlashCriticalSection
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "FlashCriticalSection.h"
#include "Logger.h"
#include "FlashCriticalFlag.h"

class FlashCritLock
{
public:
    explicit FlashCritLock(RaftMutex& mutex) : _mutex(mutex)
    {
        RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER);
    }

    ~FlashCritLock()
    {
        RaftMutex_unlock(_mutex);
    }

    FlashCritLock(const FlashCritLock&) = delete;
    FlashCritLock& operator=(const FlashCritLock&) = delete;

private:
    RaftMutex& _mutex;
};

FlashCriticalSection::FlashCriticalSection(const char* pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
    RaftMutex_init(_accessMutex);
}

FlashCriticalSection::~FlashCriticalSection()
{
    RaftMutex_destroy(_accessMutex);
}

void FlashCriticalSection::setup()
{
    FlashCriticalSectionIF::setGlobal(this);
}

void FlashCriticalSection::enterFlashCritical(const char* reason)
{
    bool notify = false;
    {
        FlashCritLock guard(_accessMutex);
        if (_refCount < 0)
            _refCount = 0;
        _refCount++;
        if (reason)
            _lastReason = reason;
        if (_refCount == 1)
            notify = true;
    }
    if (notify)
        g_flashCriticalActive = true;
    if (notify)
        notifyEnter(reason);
}

void FlashCriticalSection::exitFlashCritical(const char* reason)
{
    bool notify = false;
    {
        FlashCritLock guard(_accessMutex);
        if (_refCount > 0)
            _refCount--;
        if (reason)
            _lastReason = reason;
        if (_refCount == 0)
            notify = true;
    }
    if (notify)
        notifyExit(reason);
    if (notify)
        g_flashCriticalActive = false;
}

void FlashCriticalSection::registerListener(FlashCriticalListener* listener)
{
    if (!listener)
        return;
    FlashCritLock guard(_accessMutex);
    for (auto* existing : _listeners)
    {
        if (existing == listener)
            return;
    }
    _listeners.push_back(listener);
}

void FlashCriticalSection::unregisterListener(FlashCriticalListener* listener)
{
    if (!listener)
        return;
    FlashCritLock guard(_accessMutex);
    for (auto it = _listeners.begin(); it != _listeners.end();)
    {
        if (*it == listener)
            it = _listeners.erase(it);
        else
            ++it;
    }
}

void FlashCriticalSection::notifyEnter(const char* reason)
{
    std::vector<FlashCriticalListener*> listenersCopy;
    {
        FlashCritLock guard(_accessMutex);
        listenersCopy = _listeners;
    }
    for (auto* listener : listenersCopy)
    {
        if (listener)
            listener->onFlashCriticalEnter();
    }
    if (reason)
        LOG_I(MODULE_PREFIX, "enter reason %s", reason);
}

void FlashCriticalSection::notifyExit(const char* reason)
{
    std::vector<FlashCriticalListener*> listenersCopy;
    {
        FlashCritLock guard(_accessMutex);
        listenersCopy = _listeners;
    }
    for (auto* listener : listenersCopy)
    {
        if (listener)
            listener->onFlashCriticalExit();
    }
    if (reason)
        LOG_I(MODULE_PREFIX, "exit reason %s", reason);
}

String FlashCriticalSection::getDebugJSON() const
{
    String jsonStr = "{";
    jsonStr += "\"ref\":" + String(_refCount) + ",";
    jsonStr += "\"reason\":\"" + _lastReason + "\"";
    jsonStr += "}";
    return jsonStr;
}
