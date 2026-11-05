"""Record opt-in FuturamaPerf telemetry and memory for one already-running game process."""
import argparse
import json
import pathlib
import re
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb', default='adb')
    p.add_argument('--package', default='com.genisson.futuramarecomp.validation')
    p.add_argument('--seconds', type=int, default=45)
    p.add_argument('--output', type=pathlib.Path, required=True)
    a = p.parse_args()
    def adb(*args):
        return subprocess.check_output([a.adb, *args], text=True, timeout=20).strip()
    pid = adb('shell', 'pidof', a.package + ':game').split()[0]
    def logs():
        return adb('logcat', '-d', '--pid=' + pid, '-s', 'FuturamaPerf:I', '*:S').splitlines()
    previous = set(logs())
    memory = []
    start = time.monotonic()
    while time.monotonic() - start < a.seconds:
        report = adb('shell', 'dumpsys', 'meminfo', pid)
        memory.append({'seconds': round(time.monotonic()-start, 1),
                       'pss_kib': int(re.search(r'TOTAL PSS:\s+(\d+)', report)[1]),
                       'rss_kib': int(re.search(r'TOTAL RSS:\s+(\d+)', report)[1])})
        time.sleep(min(5, max(0, a.seconds - (time.monotonic() - start))))
    records = []
    for line in logs():
        if line in previous or 'FuturamaPerf:' not in line:
            continue
        row = {}
        for key, value in re.findall(r'(\w+)=([^ ]*)', line.split('FuturamaPerf:', 1)[1]):
            try: row[key] = float(value)
            except ValueError: row[key] = value
        records.append(row)
    result = {'duration_seconds': a.seconds, 'records': records, 'memory': memory}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    if not records:
        raise SystemExit('No frame samples collected. Check profiling, foreground gameplay and the process; memory samples alone do not validate performance.')


if __name__ == '__main__':
    main()
