# RaftSysMods Linux unit tests

Host-side tests of RaftSysMods code that doesn't need ESP32 hardware. They are run by CI
(`.github/workflows/linux-unit-tests.yml`) on every push to `main` and every pull request.

## Running

```bash
make -C linux_unit_tests check      # build and run the tests; exit code is non-zero if any fails
make -C linux_unit_tests clean
```

Requirements: `g++` with C++20 and `make`. Tests that use RaftCore code build against the RaftCore checkout next
to this one (`../../RaftCore`, as in a Raft development folder), or `RAFTCORE=<path>`.

## Tests

| Test | What it covers |
|---|---|
| `test_ble_conn_arbiter` | BLEManager's `BLEConnArbiter`: the primary connection, a session starting with a command, a second central taking over, a silent connection dropped while another is connected, per-connection subscriptions, `maxConns` 1, session counting across a handover |

## Adding a test

Write the logic to test without ESP-IDF or NimBLE dependencies (as `BLEConnArbiter.h`), add a
`test_<name>.cpp` with an `int test<Name>()` that returns its failure count (`test_harness.h`), call it from
`main.cpp` and add the file to `SOURCES` in the Makefile.
