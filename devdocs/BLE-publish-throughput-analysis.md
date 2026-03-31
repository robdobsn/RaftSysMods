# BLE Publish Throughput Analysis

## Problem

When streaming device data (devbin) over BLE using indications, occasional sample drops occur due to the outbound queue filling up and publish messages being silently discarded. WiFi shows zero drops under the same conditions.

Test results with LSM6DS3 accelerometer emulator generating a sawtooth at 104 Hz:

| Capture   | Transport | Samples | Duration | Rate     | Errors |
|-----------|-----------|---------|----------|----------|--------|
| WiFi      | WiFi      | 87,676  | 843s     | 104.0 Hz | 0      |
| BLE #1    | BLE       | 30,452  | 293s     | 104 Hz   | 2      |
| BLE #2    | BLE       | 100,000 | 1,461s   | 68.5 Hz  | 742    |
| BLE #3    | BLE       | 100,000 | 963s     | 103.9 Hz | 2      |

## Root Cause

The drops are not BLE packet loss — indicate guarantees delivery of packets that are sent. The drops occur because the firmware intentionally discards publish messages when the outbound queue is congested.

### Two-layer publish gating

1. **CommsChannelManager** (`handleOutboundMessageOnChannel`): Skips publish if any non-publish message (e.g. command response) is already queued. This rejection is silent unless `DEBUG_OUTBOUND_PUBLISH` is defined.

2. **BLEGattOutbound** (`isReadyToSend`): Rejects publish when `queueCount + outQResvNonPub >= outQMaxLen`. With defaults (`outQSize=30`, `outQResvNonPub=10`), publish can only queue when count < 20. This rejection is silent unless `WARN_ON_PUBLISH_QUEUE_FULL` is defined.

### Why the queue backs up

With `sendUseInd=true` (default), only **one indication is in flight at a time**. The code checks `_outboundMsgsInFlight > 0` and refuses to send until the ACK arrives. Despite `outMsgsInFlightMax=10` being configured, this value is never checked — the `> 0` guard enforces the BLE spec constraint (only one indication outstanding).

With `connIntvPrefMs=15`, an indication ACK takes 1-2 connection intervals (~15-30ms). This limits throughput to **~33-66 messages/second**.

### Observed message sizes

Devbin publish messages are ~330 bytes, consistent with ~4-5 poll results accumulated per publish (~200-250ms effective publish interval), not the expected 100ms from `minMs`.

### Payload breakdown (per publish at ~200ms interval)

| Component                              | Bytes |
|----------------------------------------|-------|
| DevBIN envelope (magic+topic+seq)      | 3     |
| Record length prefix (uint16 BE)       | 2     |
| Record header (bus+addr+type+seq)      | 8     |
| ~4 × (1B length + ~66B poll data)      | ~268  |
| RICSerial/HDLC framing                 | ~8    |
| **Total**                              | **~289-330** |

Each poll result contains: 2B timestamp + 4B FIFO status + ~60B FIFO data (5-6 samples × 12 bytes).

## Logging Added

Three logging points have been added (all `#ifdef`-gated, currently enabled):

| Location | Define | What it logs |
|----------|--------|-------------|
| `BLEGattOutbound.cpp` | `WARN_ON_OUTBOUND_MSG_TIMEOUT` | Indication ACK timeout (500ms) |
| `BLEGattOutbound.cpp` | `WARN_ON_PUBLISH_QUEUE_FULL` | Publish rejected by `isReadyToSend` (queue too full) |
| `CommsChannelManager.cpp` | `DEBUG_OUTBOUND_PUBLISH` | Publish skipped (other msg queued) or rejected by `outboundCanAccept` |

## Mitigation Options

### Low effort / config changes

**Reduce connection interval** (`connIntvPrefMs: 8` → 7.5ms minimum per BLE spec)
- Doubles indication throughput (~66-133 msg/s)
- Central may override and use a longer interval
- Higher power consumption on both sides

**Increase ring buffer** (`"s": 20` → 40 or more in poll config)
- Buys more headroom before overflow during stalls
- Does not fix the throughput bottleneck — just delays overflow

### Medium effort / code changes

**Request 2M PHY**
- Halves on-air time → ACK returns faster → higher indication throughput
- ESP32-S3 supports it; central must also support BLE 5.0
- Estimated ~1.5x throughput gain

**Use notify for publish, indicate for commands** (hybrid approach)
- Notifications allow multiple sends per connection event → ~3-5x throughput for devbin data
- Keep indication for command responses where reliability matters
- Currently `sendUseInd` is global; would need per-message-type switching
- Note: `minMsBetweenSends=50ms` (notification mode throttle) would also need reducing

### Higher effort

**Firmware-side decoding / payload compression**
- Decode FIFO data on firmware, send decoded samples instead of raw poll results
- Would reduce payload from ~330 bytes to ~120 bytes for the same data
- Significant design change affecting the firmware/JS decoder boundary

## Current BLE Configuration (Axiom009)

| Parameter | Value | Source |
|-----------|-------|--------|
| `sendUseInd` | true | BLEConfig.h default |
| `connIntvPrefMs` | 15ms | SysTypes.json |
| `preferredMTU` | 512 | BLEConfig.h default |
| `maxPktLen` | 500 | BLEConfig.h default |
| `outQSize` | 30 | BLEConfig.h default |
| `outQResvNonPub` | 10 | BLEConfig.h default |
| `outMsgsInFlightMax` | 10 | BLEConfig.h default (unused — hardcoded to 1) |
| `outMsgsInFlightTimeoutMs` | 500 | BLEConfig.h default |
| `minMsBetweenSends` | 50ms | BLEConfig.h default (only used in notify mode) |
| `llPacketLengthPref` | 251 | BLEConfig.h default (DLE) |
| `llPacketTimePref` | 2500μs | BLEConfig.h default |

## Throughput Estimates

| Mode | Throughput | Notes |
|------|-----------|-------|
| Indicate, 15ms CI | ~16-33 KB/s | Current setup, 1 msg per 15-30ms |
| Indicate, 7.5ms CI | ~33-66 KB/s | Reduced connection interval |
| Indicate, 7.5ms CI + 2M PHY | ~50-100 KB/s | Faster ACK round-trip |
| Notify, 15ms CI, minMs=50 | ~10 KB/s | Artificially throttled by minMsBetweenSends |
| Notify, 15ms CI, minMs=10 | ~50-100 KB/s | Multiple per connection event + DLE |
