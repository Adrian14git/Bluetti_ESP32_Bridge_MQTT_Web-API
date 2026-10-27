# MQTT measurement-round diagnostics

The bridge publishes these retained topics for the configured device ID:

- `bluetti/<device_id>/state/error_code` — `0` means the last completed round was successful; `1` means a round is in progress; nonzero error values indicate an unsuccessful/incomplete round according to the firmware's error mapping.
- `bluetti/<device_id>/state/timestamp_ms` — `millis()` value when the last successful complete round finished. This is ESP32 uptime in milliseconds, not Unix time. It remains unchanged when a round fails.
- `bluetti/<device_id>/state/round_active` — `true` while a round is running, otherwise `false`.

The three topics are published when the status changes and after MQTT reconnects. Retained values allow Home Assistant to receive the most recently published status after a restart/reconnect.

Individual measurement topics continue to be published as before. Because those topics update block by block, consumers should only treat a set of values as a new complete measurement after `error_code` returns to `0` and `timestamp_ms` advances. If a round fails, the timestamp does not advance and the error code remains nonzero until the next round begins.
