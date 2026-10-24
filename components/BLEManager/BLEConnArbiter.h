/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEConnArbiter
//
// Decides which central the peripheral serves when more than one is connected. A central such as the Windows
// Bluetooth stack can connect on its own (e.g. to refresh its cache of the services) and send no commands; while
// it is connected a peripheral that allows one connection doesn't advertise, so the central that wants a session
// can't find it. With two connections allowed (and advertising while there is a free one), the connection that
// sends a command is the session:
// - the primary connection is the one the comms channel sends on; the other is waiting for its first command
// - a connection's first command makes it the session; if it is the other connection it becomes the primary and
//   the old primary is disconnected (the newest central that talks wins)
// - while there are two connections, one that has sent no command for idleDropMs after connecting is disconnected
//
// No NimBLE dependency (so it can be host-tested). Not thread-safe: the caller holds a lock around every call.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

class BLEConnArbiter
{
public:
    static constexpr uint16_t CONN_NONE = 0xffff;   // BLE_HS_CONN_HANDLE_NONE

    // What the caller does after an event
    struct Action
    {
        uint16_t terminate = CONN_NONE;     // connection to disconnect
        bool primaryChanged = false;        // the primary connection changed (including to none)
        bool primarySubChanged = false;     // the primary's subscription to the response characteristic changed
        bool accept = true;                 // a command: deliver it (false: from a connection not tracked)
    };

    // A connection, as reported on disconnect
    enum class Role : uint8_t { None, Primary, Other };

    /// @brief Set the most connections served (1: no second connection; the caller doesn't advertise while
    ///        connected) and the idle time after which a connection that sends nothing is dropped
    void configure(uint32_t maxConns, uint32_t idleDropMs)
    {
        _maxConns = maxConns < 1 ? 1 : (maxConns > 2 ? 2 : maxConns);
        _idleDropMs = idleDropMs;
    }

    /// @brief A central connected
    Action onConnect(uint16_t connHandle, uint32_t timeNowMs)
    {
        Action action;
        if (_primary.handle == CONN_NONE)
        {
            _primary = Conn{connHandle, timeNowMs, false, false, false};
            action.primaryChanged = true;
        }
        else if ((_maxConns > 1) && (_other.handle == CONN_NONE))
        {
            _other = Conn{connHandle, timeNowMs, false, false, false};
        }
        else
        {
            action.terminate = connHandle;
        }
        return action;
    }

    /// @brief A central disconnected
    /// @param role (out) which connection it was
    /// @param connMs (out) when it connected
    Action onDisconnect(uint16_t connHandle, Role& role, uint32_t& connMs)
    {
        Action action;
        role = Role::None;
        connMs = 0;
        if (connHandle == CONN_NONE)
            return action;
        if (connHandle == _primary.handle)
        {
            role = Role::Primary;
            connMs = _primary.connMs;
            _primary = _other;
            _other = Conn{};
            action.primaryChanged = true;
        }
        else if (connHandle == _other.handle)
        {
            role = Role::Other;
            connMs = _other.connMs;
            _other = Conn{};
        }
        return action;
    }

    /// @brief A command was written by a central
    Action onCommand(uint16_t connHandle)
    {
        Action action;
        if (connHandle == _primary.handle)
        {
            if (!_primary.active)
            {
                _primary.active = true;
                _sessionCount++;
            }
        }
        else if ((connHandle != CONN_NONE) && (connHandle == _other.handle))
        {
            // The other connection talks: it becomes the primary and the session, the old primary is dropped. If the
            // old primary had a session it carries on (handed over, so not counted as a new one)
            bool hadSession = _primary.active;
            action.terminate = _primary.handle;
            _primary = _other;
            _primary.active = true;
            _other = Conn{};
            if (!hadSession)
                _sessionCount++;
            action.primaryChanged = true;
        }
        else
        {
            action.accept = false;
        }
        return action;
    }

    /// @brief A central subscribed to (or unsubscribed from) the response characteristic
    Action onSubscribe(uint16_t connHandle, bool subscribed)
    {
        Action action;
        if (connHandle == _primary.handle)
        {
            _primary.subscribed = subscribed;
            action.primarySubChanged = true;
        }
        else if (connHandle == _other.handle)
        {
            _other.subscribed = subscribed;
        }
        return action;
    }

    /// @brief Check for a connection to drop (call regularly) - each connection is returned once
    Action service(uint32_t timeNowMs)
    {
        Action action;
        if ((_primary.handle == CONN_NONE) || (_other.handle == CONN_NONE) || (_idleDropMs == 0))
            return action;
        Conn* pDrop = nullptr;
        if (!_other.active && !_other.dropRequested && isElapsed(timeNowMs, _other.connMs, _idleDropMs))
            pDrop = &_other;
        else if (!_primary.active && !_primary.dropRequested && isElapsed(timeNowMs, _primary.connMs, _idleDropMs))
            pDrop = &_primary;
        if (pDrop)
        {
            pDrop->dropRequested = true;
            action.terminate = pDrop->handle;
        }
        return action;
    }

    /// @brief Forget all connections (e.g. the BLE stack has stopped)
    void clear()
    {
        _primary = Conn{};
        _other = Conn{};
    }

    // State
    uint16_t primary() const { return _primary.handle; }
    uint16_t other() const { return _other.handle; }
    bool primarySubscribed() const { return _primary.subscribed; }
    bool isSessionActive() const { return (_primary.handle != CONN_NONE) && _primary.active; }
    uint32_t sessionCount() const { return _sessionCount; }   // sessions started (a handover isn't one)
    uint32_t numConns() const { return (_primary.handle != CONN_NONE ? 1 : 0) + (_other.handle != CONN_NONE ? 1 : 0); }
    bool canAcceptConn() const { return numConns() < _maxConns; }

private:
    struct Conn
    {
        uint16_t handle = CONN_NONE;
        uint32_t connMs = 0;
        bool active = false;        // has sent a command
        bool subscribed = false;    // to the response characteristic
        bool dropRequested = false; // returned by service() to be disconnected
    };
    Conn _primary;
    Conn _other;
    uint32_t _maxConns = 2;
    uint32_t _idleDropMs = 8000;
    uint32_t _sessionCount = 0;

    static bool isElapsed(uint32_t timeNowMs, uint32_t startMs, uint32_t periodMs)
    {
        return (uint32_t)(timeNowMs - startMs) >= periodMs;
    }
};
