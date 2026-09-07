#!/usr/bin/env bash
# Measure Dante's Inferno (or any game) running on Android via SurfaceFlinger
# compositor presentation timestamps. SurfaceFlinger keeps the last 128
# present timestamps per layer; gaps between them represent the true frame
# intervals delivered to the physical display panel.
#
# Usage:
#   ./tools/bench.sh [package] [seconds] [label]
#
# Examples:
#   ./tools/bench.sh                                    # benchmarks com.dantesinferno.game for 30s
#   ./tools/bench.sh com.dantesinferno.game 15 "Turnip" # benchmarks for 15s with label
set -uo pipefail

PKG_UNDER_TEST="${1:-com.dantesinferno.game}"
SECS="${2:-30}"
LABEL="${3:-$PKG_UNDER_TEST}"

# ADB device selection
ADB_CMD="adb"
if [ -n "${ANDROID_SERIAL:-}" ]; then
  ADB_CMD="adb -s $ANDROID_SERIAL"
elif [ -n "${SERIAL:-}" ]; then
  ADB_CMD="adb -s $SERIAL"
fi

adbs() { $ADB_CMD "$@"; }

# Verify device connectivity
if ! adbs get-state >/dev/null 2>&1; then
  echo "Error: No Android device connected via ADB."
  echo "Connect your device with USB debugging enabled, or set ANDROID_SERIAL."
  exit 1
fi

pid=$(adbs shell pidof "$PKG_UNDER_TEST" | tr -d '\r')
if [ -z "$pid" ]; then
  echo "Error: $PKG_UNDER_TEST is not running on the connected device."
  echo "Launch the game on the device and try again."
  exit 1
fi

# Detect SurfaceFlinger layer name for the app
all=$(adbs shell dumpsys SurfaceFlinger --list 2>/dev/null | tr -d '\r' \
        | grep -i "$PKG_UNDER_TEST" | grep -v 'Background for')
raw=$(printf '%s\n' "$all" | grep -F '(BLAST)' | tail -1)
[ -n "$raw" ] || raw=$(printf '%s\n' "$all" | grep -i 'SurfaceView' | tail -1)
[ -n "$raw" ] || raw=$(printf '%s\n' "$all" | tail -1)

if [ -z "$raw" ]; then
  echo "Error: No active SurfaceFlinger layer found for $PKG_UNDER_TEST."
  exit 1
fi

layer=$(printf '%s' "$raw" | sed -E 's/^RequestedLayerState\{//; s/\}$//; s/ parentId=[0-9]+.*$//')

echo "============================================================"
echo " Dante's Inferno Android Performance Benchmark"
echo "============================================================"
echo "   Package:  $PKG_UNDER_TEST (PID: $pid)"
echo "   Layer:    $layer"
echo "   Label:    $LABEL"
echo "   Sampling: ${SECS}s (play normally during this interval)"
echo "============================================================"

# Clear previous frame history
adbs shell "dumpsys SurfaceFlinger --latency-clear '$layer'" >/dev/null 2>&1

# Record starting CPU ticks
start_cpu=$(adbs shell cat /proc/$pid/stat 2>/dev/null | awk '{print $14+$15}')

# Wait for measurement window
sleep "$SECS"

# Record ending CPU ticks and metrics
end_cpu=$(adbs shell cat /proc/$pid/stat 2>/dev/null | awk '{print $14+$15}')
latency_data=$(adbs shell "dumpsys SurfaceFlinger --latency '$layer'" 2>/dev/null | tr -d '\r')
mem=$(adbs shell dumpsys meminfo "$PKG_UNDER_TEST" 2>/dev/null | tr -d '\r' | grep -E 'TOTAL PSS' | head -1)
therm=$(adbs shell dumpsys thermalservice 2>/dev/null | tr -d '\r' | grep -iE 'Temperature\{.*type=SKIN|mStatus' | head -2)
ticks=$(adbs shell getconf CLK_TCK 2>/dev/null | tr -d '\r'); ticks=${ticks:-100}

# Parse SurfaceFlinger timestamps using python3
python3 - "$LABEL" "$SECS" "$start_cpu" "$end_cpu" "$ticks" "$latency_data" <<'PY'
import sys

label = sys.argv[1]
secs = float(sys.argv[2])
c0_str = sys.argv[3]
c1_str = sys.argv[4]
ticks = float(sys.argv[5])
raw_latency = sys.argv[6]

rows = [l.split() for l in raw_latency.splitlines() if l.strip()]
if not rows:
    print("   [!] No frame data returned from SurfaceFlinger.")
    sys.exit(0)

# Line 0: refresh period in ns
# Subsequent lines: desired-present / actual-present / frame-ready in ns
present = []
for r in rows[1:]:
    if len(r) >= 3:
        try:
            t = int(r[1])
        except ValueError:
            continue
        # Ignore 0 and sentinel values
        if 0 < t < (1 << 63) - 1:
            present.append(t)

present.sort()
gaps = [(b - a) / 1e6 for a, b in zip(present, present[1:]) if 0 < (b - a) < 1e9]

print(f"\nResults for: {label}")
print(f"   Presented frames sampled: {len(present)}")

if gaps:
    gaps_sorted = sorted(gaps)
    n = len(gaps_sorted)
    p = lambda q: gaps_sorted[min(n - 1, int(n * q))]
    span = (present[-1] - present[0]) / 1e9
    fps = len(gaps) / span if span > 0 else 0.0

    print(f"   FPS (compositor present) : {fps:6.1f} fps")
    print(f"   Frametime p50 (median)   : {p(0.50):6.2f} ms")
    print(f"   Frametime p95            : {p(0.95):6.2f} ms")
    print(f"   Frametime p99            : {p(0.99):6.2f} ms")
    print(f"   Frametime Max            : {gaps_sorted[-1]:6.2f} ms")

    over_16 = sum(1 for g in gaps_sorted if g > 17.5)
    over_20 = sum(1 for g in gaps_sorted if g > 20.0)
    over_33 = sum(1 for g in gaps_sorted if g > 33.3)
    print(f"   Frames > 16.7ms (missed) : {100.0 * over_16 / n:5.1f} % ({over_16}/{n})")
    print(f"   Frames > 20.0ms (judder) : {100.0 * over_20 / n:5.1f} % ({over_20}/{n})")
    print(f"   Frames > 33.3ms (stutter): {100.0 * over_33 / n:5.1f} % ({over_33}/{n})")
else:
    print("   [!] No valid frame intervals recorded (app may be paused or not rendering).")

try:
    c0 = int(c0_str)
    c1 = int(c1_str)
    cpu_s = (c1 - c0) / ticks
    print(f"   App CPU Usage            : {100.0 * cpu_s / secs:6.1f} % of one CPU core")
except Exception:
    pass

PY

if [ -n "$mem" ]; then
  echo "   Memory (PSS)             : $(echo "$mem" | sed 's/^[ \t]*//')"
fi
if [ -n "$therm" ]; then
  echo "   Thermal Status           : $(echo "$therm" | tr '\n' ' ' | sed 's/^[ \t]*//')"
fi
echo "============================================================"
