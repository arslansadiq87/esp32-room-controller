"""Summarize monotonic D1 TRACE measurements from a saved serial log."""
import math
import re
import sys
from pathlib import Path

traces = {
    "queue_ms": r"\[TRACE\] dequeued queue_ms=(\d+)",
    "send_us": r"\[TRACE\] sent channel=\d+ send_us=(\d+)",
    "event_confirmation_us": r"\[TRACE\] event_match channel=\d+ elapsed_us=(\d+)",
    "snapshot_confirmation_us": r"\[TRACE\] snapshot_match channel=\d+ elapsed_us=(\d+)",
}
log = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
for label, pattern in traces.items():
    values = sorted(map(int,re.findall(pattern,log)))
    if not values:
        print(f"{label}: no samples")
        continue
    print(f"{label}: n={len(values)} max={values[-1]}",end="")
    for percentile in (50,95,99):
        if len(values) >= math.ceil(100 / (100-percentile)):
            index = math.ceil(percentile * len(values) / 100)-1
            print(f" p{percentile}={values[index]}",end="")
    print()
