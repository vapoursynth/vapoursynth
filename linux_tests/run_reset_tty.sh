#!/bin/sh
# Runs one of the two GPU-reset probes (L1 reset_next_submit, L7 p16_deviceloss) with the
# logging set up so the result survives a machine that does not come back.
#
#   ./run_reset_tty.sh reset_next_submit 1000000
#   ./run_reset_tty.sh p16_deviceloss
#
# Two things this exists for:
#   * logs go to $HOME, never /tmp -- /tmp is cleared on reboot and that is exactly when you
#     want the log. A previous run's evidence was lost that way.
#   * output is line buffered and fsync'd, so what was printed before a hard freeze is on disk
#     rather than sitting in a stdio buffer.
set -u

PROBE="${1:-reset_next_submit}"
ARG="${2:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${VS_PREFIX:-$HERE/../inst}"

if [ ! -x "$HERE/bin/$PROBE" ]; then
    echo "no $HERE/bin/$PROBE -- run build_probes.sh first" >&2
    exit 2
fi

VSLIB="$(find "$PREFIX" -name libvapoursynth.so -print 2>/dev/null | head -1)"
[ -n "$VSLIB" ] || { echo "libvapoursynth.so not found under $PREFIX" >&2; exit 2; }
export LD_LIBRARY_PATH="$(dirname "$VSLIB"):${LD_LIBRARY_PATH:-}"

LOGDIR="$HOME/vs-reset-logs"
mkdir -p "$LOGDIR"
LOG="$LOGDIR/$PROBE${ARG:+-$ARG}-$(date +%Y%m%d-%H%M%S).log"

{
    echo "probe:   $PROBE $ARG"
    echo "prefix:  $PREFIX"
    echo "libdir:  $(dirname "$VSLIB")"
    echo "kernel:  $(uname -r)"
    echo "started: $(date -Is)"
    echo
} | tee "$LOG"
sync

stdbuf -oL -eL "$HERE/bin/$PROBE" $ARG 2>&1 | tee -a "$LOG"
sync

echo >> "$LOG"
echo "finished: $(date -Is)" >> "$LOG"
sync
echo
echo "log: $LOG"
echo "kernel side: journalctl -k -b | grep -iE 'amdgpu|gpu reset|ring .*timeout|soft recovery'"
echo "if it needed a reboot, the same with -b -1"
