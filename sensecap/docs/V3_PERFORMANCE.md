# V3 performance measurements

The D1 emits TRACE records for queue age in milliseconds, WebSocket send duration in microseconds, and elapsed time from send completion to a matching snapshot. The latter includes snapshot scheduling and network delay. It is measured entirely on the D1 clock. Serial output can perturb timing. These probes do not measure touch-to-photon or controller processing.

Use unloaded channels for repeated switching. Collect p50, p95, p99 and maximum per stage, report timeout and reconnect counts separately, and record Wi-Fi RSSI and heap from NET records. Compare Wi-Fi power profiles and ESP-NOW only on an equivalent workload. No physical samples or soak results are available from this pass.

Run `python tests/latency-report.py panel-serial.txt` on a saved D1 serial log after a series of single-channel actions with loads disconnected. Event and snapshot confirmations are reported separately. Send-to-readback duration excludes touch and queue time. The summary only includes logged confirmations. Review unconfirmed actions and reconnect counters separately.

## Development LAN measurement

A Windows client sent 100 sequential get_status requests over a persistent WebSocket to 192.168.100.227. The initial unsolicited snapshot was drained before sampling. Each interval includes sending, receiving a full_status message and parsing its JSON. Samples were separated by 50 ms. Median 17.5 ms, p95 99.4 ms, p99 116.1 ms, maximum 177.3 ms. D1 touch, FreeRTOS scheduling, output actuation and panel rendering require separate on-device measurements.
