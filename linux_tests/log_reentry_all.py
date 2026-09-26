"""L16 regression net: every way to bring the device up, each with a log handler that re-enters.

L16 found bring-up logging the device line under vulkanDeviceLock; the fix logs after the guard.
This drives each public entry point that can create the device, in a fresh process each, with a
handler that reads core.vulkan_device_info from inside the message. Every one must complete.

    python log_reentry_all.py <entry>      entries: info | devices | upload | select
"""
import logging
import sys
import threading

import vapoursynth as vs

core = vs.core
entry = sys.argv[1]
seen = []


class Handler(logging.Handler):
    def emit(self, record):
        seen.append((threading.get_ident(), record.levelname, record.getMessage()[:70]))
        try:
            core.vulkan_device_info
        except Exception as e:
            seen.append(("reenter raised", str(e)[:70]))


log = logging.getLogger("vapoursynth")
log.setLevel(logging.DEBUG)
log.addHandler(Handler())

if entry == "info":
    core.vulkan_device_info
elif entry == "devices":
    core.vulkan_devices
elif entry == "upload":
    with core.std.GPUDownload(core.std.GPUUpload(core.std.BlankClip(width=64, height=64))).get_frame(0):
        pass
elif entry == "select":
    core.set_vulkan_device(0)
    core.vulkan_device_info
else:
    sys.exit(2)
msgs = sum(1 for s in seen if isinstance(s[0], int))
print(f"RESULT {entry}: completed; {msgs} message(s) reached the handler during bring-up", flush=True)
