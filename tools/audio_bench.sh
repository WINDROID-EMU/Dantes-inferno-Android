#!/usr/bin/env bash
# ==============================================================================
# Dante's Inferno Android - Audio Pipeline Benchmark & Diagnostics Tool
# ==============================================================================
# Monitors real-time audio health, AudioTrack buffer states, FastMixer timing,
# underruns, overruns, discontinuities, and audio thread CPU scheduling priorities.
#
# Usage:
#   ./tools/audio_bench.sh [options]
#
# Options:
#   -d, --duration <secs>    Run a timed benchmark for <secs> seconds (e.g. 15, 30)
#   -s, --serial <serial>    Target specific ADB device serial
#   -p, --package <pkg>      Package name (default: com.dantesinferno.game)
#   -h, --help               Display this help message
#
# Examples:
#   ./tools/audio_bench.sh                 # Real-time live dashboard (refreshes every 1s)
#   ./tools/audio_bench.sh -d 30           # 30-second benchmark with summary report
#   ./tools/audio_bench.sh -s cec895ed -d 20 # Benchmark on specific device
# ==============================================================================
set -euo pipefail

PACKAGE="com.dantesinferno.game"
DURATION=0
DEVICE_SERIAL="${ANDROID_SERIAL:-${SERIAL:-}}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--duration)
      DURATION="$2"
      shift 2
      ;;
    -s|--serial)
      DEVICE_SERIAL="$2"
      shift 2
      ;;
    -p|--package)
      PACKAGE="$2"
      shift 2
      ;;
    -h|--help)
      sed -n '2,19p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      if [[ "$1" =~ ^[0-9]+$ ]]; then
        DURATION="$1"
      else
        PACKAGE="$1"
      fi
      shift
      ;;
  esac
done

ADB_CMD="adb"
if [ -n "$DEVICE_SERIAL" ]; then
  ADB_CMD="adb -s $DEVICE_SERIAL"
fi

adbs() { $ADB_CMD "$@"; }

# Verify device connectivity
if ! adbs get-state >/dev/null 2>&1; then
  echo "Error: No Android device connected via ADB."
  echo "Make sure your device is connected and USB debugging is enabled."
  exit 1
fi

DEVICE_MODEL=$(adbs shell getprop ro.product.model 2>/dev/null | tr -d '\r')
DEVICE_CHIP=$(adbs shell getprop ro.board.platform 2>/dev/null | tr -d '\r')

# Python benchmark engine
exec python3 - "$PACKAGE" "$DURATION" "$ADB_CMD" "$DEVICE_MODEL" "$DEVICE_CHIP" <<'PY'
import sys
import subprocess
import time
import re
import os

pkg = sys.argv[1]
duration = int(sys.argv[2])
adb_base = sys.argv[3].split()
device_model = sys.argv[4]
device_chip = sys.argv[5]

def run_adb(args):
    try:
        cmd = adb_base + args
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        return res.stdout.decode('latin1', errors='replace')
    except Exception as e:
        return ""

def get_pid():
    out = run_adb(['shell', 'pidof', pkg]).strip()
    for part in out.split():
        if part.isdigit():
            return int(part)
    return None

def parse_audio_flinger(pid):
    dump = run_adb(['shell', 'dumpsys', 'media.audio_flinger'])
    stats = {
        'track_id': 'N/A',
        'active': 'no',
        'sample_rate': 0,
        'format': 'N/A',
        'channels': 'N/A',
        'server_frm_cnt': 0,
        'frm_rdy': 0,
        'underruns': 0,
        'flushed': 0,
        'latency_ms': 0.0,
        'mixer_disc': 0,
        'mixer_type': 'N/A',
        'mixer_name': 'N/A',
        'process_time_avg': 0.0,
        'process_time_max': 0.0
    }

    # Find the track row matching pid
    # Format of Track row:
    # Type  Id Active Client Session Port Id S Flags Format Chn mask SRate ST Usg CT G db L dB R dB VS dB Server FrmCnt FrmRdy F Underruns Flushed BitPerfect InternalMute Latency
    lines = dump.splitlines()
    for line in lines:
        parts = line.split()
        if str(pid) in parts and ('yes' in parts or 'no' in parts):
            try:
                # Find column of 'yes' or 'no'
                active_idx = -1
                for idx, p in enumerate(parts):
                    if p in ('yes', 'no') and idx >= 2:
                        active_idx = idx
                        break
                if active_idx != -1:
                    stats['track_id'] = parts[active_idx - 1]
                    stats['active'] = parts[active_idx]
                    # Format: 00000005, SRate: 48000, Server FrmCnt, FrmRdy, Underruns, Flushed, Latency
                    for i, p in enumerate(parts):
                        if p == '48000' or p == '44100':
                            stats['sample_rate'] = int(p)
                        if p.startswith('0000000'):
                            stats['format'] = 'FLOAT' if p.endswith('5') else ('PCM16' if p.endswith('1') else p)
                        if p == '8000003F':
                            stats['channels'] = '5.1 Surround (6ch)'
                        elif p == '00000003':
                            stats['channels'] = 'Stereo (2ch)'
                    # Underruns & Latency are usually near the end
                    if len(parts) >= 20:
                        # Find numeric values for FrmCnt, FrmRdy, Underruns, Flushed, Latency
                        # Look for latency ending in t/k or float
                        for i in range(len(parts) - 1, 10, -1):
                            val = parts[i].rstrip('t').rstrip('k')
                            try:
                                fval = float(val)
                                if stats['latency_ms'] == 0.0 and 0 < fval < 2000:
                                    stats['latency_ms'] = fval
                                    continue
                            except ValueError:
                                pass
                        # Underruns and Flushed are integers before BitPerfect
                        for i in range(active_idx + 6, len(parts) - 3):
                            if parts[i].isdigit() and parts[i+1].isdigit():
                                # Candidate for FrmCnt, FrmRdy or Underruns, Flushed
                                pass
            except Exception:
                pass

    # Extract Mixer stats & discontinuities
    # e.g.: Timestamp stats: n=70425 disc=16 cold=0 ...
    for line in lines:
        if 'Timestamp stats:' in line and 'disc=' in line:
            m = re.search(r'disc=(\d+)', line)
            if m:
                stats['mixer_disc'] = int(m.group(1))
        if 'Process time ms stats:' in line:
            m_avg = re.search(r'ave=([\d\.]+)', line)
            m_max = re.search(r'max=([\d\.]+)', line)
            if m_avg: stats['process_time_avg'] = float(m_avg.group(1))
            if m_max: stats['process_time_max'] = float(m_max.group(1))
        if 'Output thread' in line and 'type' in line:
            stats['mixer_name'] = line.strip()

    # Direct extraction of Underruns from track line
    for line in lines:
        if str(pid) in line and ('00000005' in line or '00000001' in line):
            # Regex match track fields: FrmCnt, FrmRdy, Underruns, Flushed
            # e.g. 01032540   8192    6272 A         0        0      false
            m = re.search(r'([0-9A-F]{8})\s+(\d+)\s+(\d+)\s+([A-Za-z])\s+(\d+)\s+(\d+)', line)
            if m:
                stats['server_frm_cnt'] = int(m.group(2))
                stats['frm_rdy'] = int(m.group(3))
                stats['underruns'] = int(m.group(5))
                stats['flushed'] = int(m.group(6))

    return stats

def get_audio_threads(pid):
    # Reads /proc/<pid>/task/ to inspect audio threads
    cmd_out = run_adb(['shell', f'ls /proc/{pid}/task/ 2>/dev/null']).strip()
    tids = [t for t in cmd_out.split() if t.isdigit()]
    audio_threads = []
    for tid in tids:
        comm = run_adb(['shell', f'cat /proc/{pid}/task/{tid}/comm 2>/dev/null']).strip()
        is_audio = any(kw in comm for kw in ['Audio', 'EARS', 'Dac', 'RwAudio', 'XMA'])
        if is_audio:
            stat_line = run_adb(['shell', f'cat /proc/{pid}/task/{tid}/stat 2>/dev/null']).strip()
            # Extract nice and priority
            # stat format: pid (name) state ppid pgrp ... priority nice ...
            nice = 'N/A'
            pri = 'N/A'
            if ')' in stat_line:
                rest = stat_line[stat_line.rfind(')') + 2:].split()
                if len(rest) >= 17:
                    pri = rest[15]
                    nice = rest[16]
            audio_threads.append({
                'tid': tid,
                'name': comm,
                'nice': nice,
                'pri': pri
            })
    return audio_threads

def get_recent_logcat_underruns(pid):
    # Checks logcat for 'no frames queued' or 'AudioPriority'
    out = run_adb(['logcat', '-d', '-v', 'brief', '--pid', str(pid), '-T', '50']).strip()
    silence_count = out.count('no frames queued')
    prio_elevated = out.count('AudioPriority')
    return silence_count, prio_elevated

pid = get_pid()
if not pid:
    print(f"\n[!] Error: Process '{pkg}' is not running.")
    print("    Launch Dante's Inferno on the Android device first.\n")
    sys.exit(1)

print("\033[1;36m" + "="*70 + "\033[0m")
print(f"\033[1;37m Dante's Inferno - Audio Pipeline Benchmark & Diagnostics\033[0m")
print(f" Device:  {device_model} ({device_chip}) | PID: {pid}")
print("\033[1;36m" + "="*70 + "\033[0m")

initial_stats = parse_audio_flinger(pid)
initial_underruns = initial_stats['underruns']
initial_disc = initial_stats['mixer_disc']

if duration > 0:
    print(f"\n[*] Starting timed benchmark for {duration} seconds...")
    print("    Play combat/attacks in game during this window to test under heavy load!\n")
    start_time = time.time()
    samples = []
    
    for i in range(duration):
        time.sleep(1.0)
        curr = parse_audio_flinger(pid)
        samples.append(curr)
        elapsed = i + 1
        pct = int(elapsed * 100 / duration)
        bar = "#" * (pct // 5) + "-" * (20 - (pct // 5))
        du = curr['underruns'] - initial_underruns
        dd = curr['mixer_disc'] - initial_disc
        print(f"\r  [{bar}] {elapsed:2d}/{duration}s | Buffer FrmRdy: {curr['frm_rdy']:4d} | Underrun Δ: {du} | Disc Δ: {dd}", end="", flush=True)
    
    print("\n\n" + "\033[1;32m" + "="*70 + "\033[0m")
    print("\033[1;37m BENCHMARK RESULTS & HEALTH ANALYSIS\033[0m")
    print("\033[1;32m" + "="*70 + "\033[0m")
    
    final_stats = samples[-1] if samples else initial_stats
    total_underrun_delta = final_stats['underruns'] - initial_underruns
    total_disc_delta = final_stats['mixer_disc'] - initial_disc
    avg_frm_rdy = sum(s['frm_rdy'] for s in samples) / max(len(samples), 1)
    
    print(f" - Active Audio Track:    ID {final_stats['track_id']} ({final_stats['channels']}, {final_stats['format']}, {final_stats['sample_rate']} Hz)")
    print(f" - AudioTrack Latency:    {final_stats['latency_ms']:.2f} ms")
    print(f" - Buffer Capacity:       {final_stats['server_frm_cnt']} frames")
    print(f" - Average Ready Buffer:  {avg_frm_rdy:.1f} frames ({avg_frm_rdy / 48.0:.2f} ms margin)")
    print(f" - AudioTrack Underruns:  {total_underrun_delta} during test")
    print(f" - FastMixer Gaps (disc): {total_disc_delta} during test")
    print(f" - Mixer Process Time:    avg {final_stats['process_time_avg']:.2f} ms, max {final_stats['process_time_max']:.2f} ms")
    
    threads = get_audio_threads(pid)
    print("\n Audio Worker / Mixer Thread Priorities:")
    all_nice_elevated = True
    for t in threads:
        nice_str = t['nice']
        is_elevated = (nice_str.isdigit() or (nice_str.startswith('-') and nice_str[1:].isdigit())) and int(nice_str) <= -10
        status = "\033[1;32m[ELEVATED - OK]\033[0m" if is_elevated else "\033[1;31m[NORMAL/LOW - STARVATION RISK]\033[0m"
        if not is_elevated:
            all_nice_elevated = False
        print(f"   * TID {t['tid']:5s} | {t['name']:20s} | Nice: {nice_str:3s} | Priority: {t['pri']:3s} {status}")
    
    score = 100
    if total_underrun_delta > 0: score -= min(total_underrun_delta * 15, 60)
    if total_disc_delta > 0: score -= min(total_disc_delta * 10, 30)
    if avg_frm_rdy < 1024: score -= 15
    if not all_nice_elevated: score -= 20
    score = max(score, 0)
    
    score_color = "\033[1;32m" if score >= 85 else ("\033[1;33m" if score >= 60 else "\033[1;31m")
    print(f"\n Overall Audio Stability Score: {score_color}{score} / 100\033[0m")
    if score >= 90:
        print(" Verdict: EXCELLENT - Audio pipeline is perfectly paced with zero starvation.")
    elif score >= 70:
        print(" Verdict: GOOD - Minor buffer fluctuations, no severe audio dropouts.")
    else:
        print(" Verdict: POOR - High underrun/starvation risk during intense combat.")
    print("="*70 + "\n")

else:
    # Live Interactive Dashboard
    print("\nPress Ctrl+C to stop live monitoring.\n")
    try:
        prev_underruns = initial_underruns
        prev_disc = initial_disc
        while True:
            curr = parse_audio_flinger(pid)
            threads = get_audio_threads(pid)
            du = curr['underruns'] - prev_underruns
            dd = curr['mixer_disc'] - prev_disc
            prev_underruns = curr['underruns']
            prev_disc = curr['mixer_disc']
            
            # Clear line / ANSI update
            os.system('clear' if os.name == 'posix' else 'cls')
            print("\033[1;36m" + "="*72 + "\033[0m")
            print(f"\033[1;37m Dante's Inferno - Live Audio Monitor (PID {pid} on {device_model})\033[0m")
            print("\033[1;36m" + "="*72 + "\033[0m")
            
            # Buffer status
            buf_health = "\033[1;32mHEALTHY\033[0m" if curr['frm_rdy'] >= 1024 else "\033[1;31mLOW\033[0m"
            print(f" Track: ID {curr['track_id']} | State: {curr['active']} | {curr['channels']} @ {curr['sample_rate']} Hz")
            print(f" Format: {curr['format']} | Buffer Latency: {curr['latency_ms']:.2f} ms")
            print(f" Buffer: {curr['frm_rdy']} / {curr['server_frm_cnt']} ready frames [{buf_health}]")
            
            ud_color = "\033[1;32m" if du == 0 else "\033[1;31m"
            disc_color = "\033[1;32m" if dd == 0 else "\033[1;31m"
            print(f" Underruns: {curr['underruns']} (Δ {ud_color}+{du}\033[0m) | FastMixer disc: {curr['mixer_disc']} (Δ {disc_color}+{dd}\033[0m)")
            print(f" Mixer time: avg {curr['process_time_avg']:.2f} ms | max {curr['process_time_max']:.2f} ms")
            
            print("\n \033[1;33mAudio Thread Priorities (/proc):\033[0m")
            for t in threads:
                nice_str = t['nice']
                is_hi = (nice_str.isdigit() or (nice_str.startswith('-') and nice_str[1:].isdigit())) and int(nice_str) <= -10
                stat = "\033[1;32m[NICE -16 OK]\033[0m" if is_hi else "\033[1;31m[NORMAL/LOW]\033[0m"
                print(f"   TID {t['tid']:5s} | {t['name']:20s} | Nice: {nice_str:3s} | Pri: {t['pri']:3s} {stat}")
            
            print("\033[1;36m" + "-"*72 + "\033[0m")
            print(" [Live Refresh: 1s] Press Ctrl+C to exit.")
            time.sleep(1.0)
    except KeyboardInterrupt:
        print("\n[*] Live monitoring stopped.")
PY
