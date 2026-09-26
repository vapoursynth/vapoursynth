"""L16: a log handler that touches the device from inside a message logged during bring-up.

vulkanDevice() takes vulkanDeviceLock -- a non-recursive std::mutex -- as its first line, and
section 2 records that bring-up logs the device line while holding it. The Python binding releases
the GIL around device calls, which covers the CROSS-thread case the comment in vapoursynth.pyx
describes. This is the SAME-thread case: the log message reaches the Python handler on the thread
that holds the lock, and a handler that decorates its records with core.vulkan_device_info re-enters
vulkanDevice() on that thread.

Run under a timeout: exit 124 is the deadlock. Two modes so a hang is attributable:
    control   handler only records what it saw       -> must complete, shows what bring-up logs
    reenter   handler also reads vulkan_device_info  -> the question
"""
import logging
import sys
import threading

import vapoursynth as vs

core = vs.core
mode = sys.argv[1] if len(sys.argv) > 1 else "control"
seen = []


class Handler(logging.Handler):
    def emit(self, record):
        seen.append((threading.get_ident(), record.levelname, record.getMessage()[:90]))
        if mode == "reenter":
            try:
                core.vulkan_device_info  # re-enters vulkanDevice() on the logging thread
            except Exception as e:  # an error is a fine answer; a hang is the finding
                seen.append(("reenter raised", str(e)[:90]))


log = logging.getLogger("vapoursynth")
log.setLevel(logging.DEBUG)  # the default WARNING would drop an INFO/DEBUG device line before any handler ran
log.addHandler(Handler())

print(f"L16 {mode}: main thread {threading.get_ident()}, bringing the device up", flush=True)
info = core.vulkan_device_info
print(f"RESULT: bring-up returned ({info['name']}); {len(seen)} handler call(s) during it", flush=True)
for s in seen:
    print("   ", s, flush=True)
if mode == "reenter" and not any(isinstance(s[0], int) for s in seen):
    print("NOTE: no message reached the handler during bring-up, so the re-entry never happened here", flush=True)
