#!/usr/bin/env python3
"""Capture and measure the reference views of the lighting and materials plan (tools/lighting/reference_views.yml;
docs/LIGHTING_AND_MATERIALS_ARCHITECTURE.md, phase 0).

Each view runs in its own isolated desktop game (tools/run_game.sh), one at a time: a fixed camera, optional console
commands at gameplay start, a screenshot, and GPU/CPU frame times from the run's measurement log (median of the
per-second p50 samples, the first one dropped). Results go to <out>/<label>/: one PNG per view and results.json, plus
a results.md table. Other openyamm processes are sampled every 2 s while a view runs (they share the GPU): a view that
overlapped one is re-run once and, if that overlaps too, marked contended in results.json and starred in results.md.
Compare two labels with --compare A B (frame times side by side, and the mean absolute pixel
difference of each view's screenshot).
Usage: reference_views.py <label> [--views a,b] [--out output/lighting_reference] [--seconds 12]
       reference_views.py --compare <label A> <label B> [--out ...]"""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
import time

import yaml

ROOT = Path(__file__).resolve().parents[2]
# The settings the 2026-10-16 baseline was captured with.
PINNED_SETTINGS = ['video.terrain_decorations=true', 'video.colored_lights=true', 'video.shadows=false',
                   'video.ambient_occlusion=false', 'video.cinematic_grading=true', 'video.sharpening=0']


GAME_PATTERN = '^[^ ]*build/game/openyamm( |$)'


def game_running():
    return subprocess.run(['pgrep', '-f', GAME_PATTERN], capture_output=True).returncode == 0


def descends_from(pid, ancestor):
    while pid > 1:
        if pid == ancestor:
            return True
        try:
            pid = int(Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[1])
        except (OSError, IndexError, ValueError):
            return False
    return False


def run_uncontended(command):
    """Runs the game; returns its output and whether another game ran at any sampled moment (shared GPU)."""
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    contended = False
    while True:
        try:
            output = process.communicate(timeout=2)[0]
            return output, contended
        except subprocess.TimeoutExpired:
            pids = subprocess.run(['pgrep', '-f', GAME_PATTERN], capture_output=True, text=True).stdout.split()
            contended = contended or any(not descends_from(int(pid), process.pid) for pid in pids)


def console_tour(lines):
    steps = [{'expect': 'gameplay'}, {'key': '`'}]
    for line in lines:
        steps += [{'text': line}, {'key': 'Return'}]
    steps += [{'key': '`'}, {'expect': 'gameplay'}]
    return {'output_dir': str(Path(tempfile.gettempdir()) / 'openyamm_reference_views'), 'exit': False, 'steps': steps}


def measure(view, folder, seconds, retry=True):
    """Run one view; returns its result record. A run that overlapped another game is re-run once and stays marked
    contended if that overlaps too."""
    shot = folder / f"{view['name']}.png"
    with tempfile.TemporaryDirectory() as scratch:
        # Console lines are typed at gameplay start; the screenshot and the measurement wait until they are done.
        settle = 4 + 2 * len(view.get('console', []))
        command = [str(ROOT / 'tools/run_game.sh'), '--isolated', f"refview_{view['name']}", '--world', view['world'],
                   '--map', view['map'], '--warmup', str(settle + 3), '--seconds', str(seconds),
                   '--set', f'debug.screenshot_path={shot}', '--set', f'debug.screenshot_delay_seconds={settle}']
        # Pinned so captures compare across runs whatever the developer's settings.ini holds now.
        for setting in PINNED_SETTINGS:
            command += ['--set', setting]
        if 'position' in view:
            command += ['--position', *[str(value) for value in view['position']],
                        '--yaw-radians', str(view.get('yaw', 0)), '--pitch-degrees', str(view.get('pitch', 0))]
        if view.get('console'):
            tour = Path(scratch) / 'console.yml'
            tour.write_text(yaml.safe_dump(console_tour(view['console'])))
            command += ['--set', f'debug.menu_input_tour_path={tour}']
        while game_running():
            time.sleep(2)
        output, contended = run_uncontended(command)
    if contended and retry:
        return measure(view, folder, seconds, False)
    record = re.search(r'Run record: (\S+)', output)
    log = Path(record.group(1)).with_name('measurement.log') if record else None
    text = log.read_text(errors='ignore') if log and log.exists() else ''
    gpu = [int(value) for value in re.findall(r'gpu_p50_us=(\d+)', text)][1:]
    cpu = [int(value) for value in re.findall(r'cpu_p50_us=(\d+)', text)][1:]
    return {'name': view['name'], 'gpu_p50_us': statistics.median(gpu) if gpu else None,
            'cpu_p50_us': statistics.median(cpu) if cpu else None, 'samples': len(gpu),
            'screenshot': shot.name if shot.exists() else None, 'contended': contended}


def compare(out, left, right):
    from PIL import Image, ImageChops, ImageStat
    a = json.loads((out / left / 'results.json').read_text())
    b = {row['name']: row for row in json.loads((out / right / 'results.json').read_text())}
    print(f"| view | GPU {left} | GPU {right} | CPU {left} | CPU {right} | pixel diff |")
    print('|---|---:|---:|---:|---:|---:|')
    for row in a:
        other = b.get(row['name'])
        if not other:
            continue
        diff = ''
        if row['screenshot'] and other['screenshot']:
            one = Image.open(out / left / row['screenshot']).convert('RGB')
            two = Image.open(out / right / other['screenshot']).convert('RGB').resize(one.size)
            diff = f"{sum(ImageStat.Stat(ImageChops.difference(one, two)).mean) / 3:.2f}"
        print(f"| {row['name']} | {row['gpu_p50_us']} | {other['gpu_p50_us']} | {row['cpu_p50_us']} | "
              f"{other['cpu_p50_us']} | {diff} |")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('label', nargs='?')
    parser.add_argument('--views')
    parser.add_argument('--out', type=Path, default=ROOT / 'output/lighting_reference')
    parser.add_argument('--seconds', type=int, default=12)
    parser.add_argument('--compare', nargs=2, metavar=('A', 'B'))
    args = parser.parse_args()
    if args.compare:
        compare(args.out, *args.compare)
        return
    views = yaml.safe_load((ROOT / 'tools/lighting/reference_views.yml').read_text())['views']
    if args.views:
        wanted = set(args.views.split(','))
        views = [view for view in views if view['name'] in wanted]
    folder = (args.out / args.label).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    results = []
    for view in views:
        result = measure(view, folder, args.seconds)
        results.append(result)
        print(f"{result['name']:28} GPU {result['gpu_p50_us']} us  CPU {result['cpu_p50_us']} us  "
              f"samples {result['samples']}  shot {result['screenshot']}"
              f"{'  CONTENDED' if result['contended'] else ''}", flush=True)
    (folder / 'results.json').write_text(json.dumps(results, indent=1) + '\n')
    lines = ['| view | GPU p50 (us) | CPU p50 (us) |', '|---|---:|---:|']
    lines += [f"| {row['name']}{' *' if row['contended'] else ''} | {row['gpu_p50_us']} | {row['cpu_p50_us']} |"
              for row in results]
    if any(row['contended'] for row in results):
        lines += ['', '\\* Another game ran during the measurement, twice: GPU times are not comparable.']
    (folder / 'results.md').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
