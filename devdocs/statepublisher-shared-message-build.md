# StatePublisher: build each topic's message once, not once per subscriber

Proposed 2026-11-22. Not started.

## Background

On SandBot (ESP32-S3), building the status message was slow enough that,
with several clients subscribed, the main loop couldn't keep a motion queue
fed and the ball's speed became erratic. The full investigation is in the
SandBot repo: `SandBotFirmware/devdocs/MAIN_LOOP_PERFORMANCE.md`.

Making the message cheap fixed most of it (7.5 ms → 0.43 ms). The rest of the
cost was multiplication: StatePublisher builds the same message separately for
each subscriber.

## What happens now

In `StatePublisher::loop()` every `Subscription` is handled on its own:

- When its check interval has elapsed, it calls `sub._stateDetectFn(topicIndex, hash)`.
- When it's time to publish, `publishData(sub)` calls
  `sub._msgGenFn(topicIndex, endpointMsg)` to build the payload into a
  `CommsChannelMsg` for that subscriber's channel.

N clients on the same topic means N state hashes and N message builds per
interval, all producing the same bytes. Each WebUI tab is a client. Before
the recent fix that removes subscriptions whose connection has gone, page
reloads made N grow without limit.

## Proposal

Cache the generated payload per topic (per `PubSource`) for a short period
and reuse it for every subscriber that publishes within it.

- Add to `PubSource`: the last payload bytes, the time built, the state hash
  at that time, and a valid flag.
- In `publishData(sub)`: if the topic's cache is younger than
  `sharedMsgMaxAgeMs` (default about 20–30 ms, configurable, 0 = off), copy
  the cached bytes into the subscriber's `CommsChannelMsg`. Otherwise call
  `_msgGenFn` and refresh the cache.
- Cache `_stateDetectFn` results the same way, so change detection runs once
  per topic per pass, not once per subscriber.
- The channel ID and message header are per subscriber, so cache the payload
  only, not the whole `CommsChannelMsg`.

**Edge case:** a generator whose output depends on the channel, for example a
binary versus a JSON variant chosen per interface. Make sharing opt-in per
source, through a flag on `registerDataSource`, or key the cache on the
message protocol as well as the topic.

## Evidence

SandBot does this at application level (`PUBLISH_SHARE_MS` = 30 in
`SandBot.cpp`). With four clients the shared build took the main loop from
starved (queue 1–4 blocks, ball crawling) to steady (queue 30–51, 294–302
mm/s at 300 mm/s commanded, measured by camera). A share counter showed most
publishes were served from the cache once two or more clients were
connected.

Doing it in StatePublisher gives the same result to every Raft app without
each one adding its own cache.

## Also worth checking

- `DEBUG_STATEPUB_OUTPUT_PUBLISH_STATS` already times hash and publish
  attempts. Add a count of generator calls against publishes so the saving
  can be seen in the field.
- WiFi transmit work showed up on the main loop's core as about 3.5 ms per
  message sent. SandBot moved WiFi and lwIP to core 1
  (`CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1`,
  `CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU1`). Worth recommending in the Raft
  docs for apps with a busy main loop.

## Related

- RaftCore `devdocs/json-and-string-hot-path-costs.md`: why the message
  builders themselves were slow.
