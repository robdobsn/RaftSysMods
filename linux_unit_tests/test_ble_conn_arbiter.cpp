/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BLEConnArbiter: which central is served when more than one is connected (BLEManager)
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "test_harness.h"
#include "BLEConnArbiter.h"

using Role = BLEConnArbiter::Role;
static const uint16_t NONE = BLEConnArbiter::CONN_NONE;

int testBLEConnArbiter()
{
    printf("Running testBLEConnArbiter...\n");
    int failCount = 0;

    // A central alone (e.g. a Windows Bluetooth stack cache visit) is never dropped; a later one that talks is the
    // session
    {
        BLEConnArbiter a;
        Role role;
        uint32_t connMs;
        BLEConnArbiter::Action x = a.onConnect(1, 0);
        TEST_ASSERT(x.primaryChanged && (a.primary() == 1) && !a.isSessionActive() && a.canAcceptConn(), "first connection is the primary, no session yet");
        TEST_ASSERT(a.service(20000).terminate == NONE, "a connection alone is never dropped");
        a.onDisconnect(1, role, connMs);
        TEST_ASSERT((role == Role::Primary) && (a.primary() == NONE), "primary disconnected");
        a.onConnect(2, 30000);
        x = a.onCommand(2);
        TEST_ASSERT(x.accept && a.isSessionActive() && (a.sessionCount() == 1), "a command starts a session");
    }

    // A silent primary; a second central connects and talks: it takes over and the silent one is dropped
    {
        BLEConnArbiter a;
        Role role;
        uint32_t connMs;
        a.onConnect(1, 0);
        a.onConnect(2, 1000);
        TEST_ASSERT(!a.canAcceptConn() && (a.numConns() == 2), "two connections: no more accepted");
        a.onSubscribe(2, true);
        TEST_ASSERT(!a.primarySubscribed(), "the other's subscription doesn't touch the primary's");
        BLEConnArbiter::Action sub = a.onSubscribe(1, true);
        TEST_ASSERT(sub.primarySubChanged && !sub.primaryChanged, "the primary's subscription is reported as such");
        a.onSubscribe(1, false);
        BLEConnArbiter::Action x = a.onCommand(2);
        TEST_ASSERT(x.accept && (x.terminate == 1) && x.primaryChanged && (a.primary() == 2), "the talking connection takes over");
        TEST_ASSERT(a.primarySubscribed() && a.isSessionActive(), "with its own subscription, as the session");
        x = a.onDisconnect(1, role, connMs);
        TEST_ASSERT((role == Role::None) && !x.primaryChanged && (a.primary() == 2), "the dropped one leaving changes nothing");
        TEST_ASSERT(a.canAcceptConn(), "a connection free again");
    }

    // A session running; a silent central connects: dropped after idleDropMs, once; the session is kept
    {
        BLEConnArbiter a;
        Role role;
        uint32_t connMs;
        a.onConnect(1, 0);
        a.onCommand(1);
        a.onConnect(2, 10000);
        TEST_ASSERT(a.service(17000).terminate == NONE, "not before idleDropMs");
        TEST_ASSERT(a.service(18000).terminate == 2, "the silent one is dropped after idleDropMs");
        TEST_ASSERT(a.service(18500).terminate == NONE, "and asked for once");
        a.onDisconnect(2, role, connMs);
        TEST_ASSERT((role == Role::Other) && (connMs == 10000) && (a.primary() == 1) && a.isSessionActive(), "the session is kept");
    }

    // Both silent: the primary is dropped first once idle, and the other becomes the primary when it leaves
    {
        BLEConnArbiter a;
        Role role;
        uint32_t connMs;
        a.onConnect(1, 0);
        a.onConnect(2, 5000);
        TEST_ASSERT(a.service(8000).terminate == 1, "the idle primary is dropped");
        BLEConnArbiter::Action x = a.onDisconnect(1, role, connMs);
        TEST_ASSERT(x.primaryChanged && (a.primary() == 2) && !a.isSessionActive(), "the other becomes the primary");
    }

    // A third connection is refused, and commands from connections not tracked are dropped
    {
        BLEConnArbiter a;
        a.onConnect(1, 0);
        a.onConnect(2, 0);
        TEST_ASSERT(a.onConnect(3, 0).terminate == 3, "a third connection is refused");
        TEST_ASSERT(!a.onCommand(3).accept && !a.onCommand(NONE).accept, "commands from untracked connections are dropped");
    }

    // One connection allowed (the previous behaviour)
    {
        BLEConnArbiter a;
        a.configure(1, 8000);
        a.onConnect(1, 0);
        TEST_ASSERT(!a.canAcceptConn() && (a.onConnect(2, 0).terminate == 2), "maxConns 1: a second connection is refused");
    }

    // Sessions are counted, not commands; a handover from a session isn't a new one; a takeover from a silent
    // primary is
    {
        BLEConnArbiter a;
        Role role;
        uint32_t connMs;
        a.onConnect(1, 0);
        a.onCommand(1);
        a.onCommand(1);
        TEST_ASSERT(a.sessionCount() == 1, "one session for several commands");
        a.onConnect(2, 100);
        a.onCommand(2);
        TEST_ASSERT((a.sessionCount() == 1) && a.isSessionActive() && (a.primary() == 2), "a handover keeps the session");
        a.onConnect(3, 200);
        BLEConnArbiter::Action x = a.onDisconnect(2, role, connMs);
        TEST_ASSERT(x.primaryChanged && (a.primary() == 3) && !a.isSessionActive(), "the session's connection leaving promotes the other, silent");

        BLEConnArbiter b;
        b.onConnect(1, 0);
        b.onConnect(2, 100);
        b.onCommand(2);
        TEST_ASSERT((b.sessionCount() == 1) && b.isSessionActive(), "a takeover from a silent primary starts a session");
    }
    return failCount;
}
