#!/usr/bin/env python3
"""Records a run with --record, replays the recording with --input, and checks that the replay is the run: the same
frames (frames.tsv's every row: the video frame, NitroSDK's VBlank count, GFL's frame count, the game frame and each
screen's hash), the same memory dumps, the same seeds and clock in run.txt, and the same save files byte for byte
(port/app/record.h; port/tools/oracle/README.md, "Recordings"). Then it records the replay too, and checks that this
second recording says what the first one does: a recording of a recording is itself, whatever the first run's input
counted (game or video frames) and whatever clock it was given.

    check_record.py [--input FILE] [--header LINE]... [--fixed-time TIME] [--expect LINE]... [--seeds N] [--save]
                    -- PROGRAM ARGUMENT...

PROGRAM gets ARGUMENT, then --run-dir and --save-dir, each run a new empty save directory, so every run starts from no
save. The first run plays FILE when given (else it records nothing but the window's input, which --headless has none
of: a live recording, with the host's time and 64 seeds from the host), with each --header line added after its first
line (a "seed" line or "clock pinned", which the shared input files of port/tests/oracle may not have), and with
--fixed-time TIME, which only the first run gets: the replay must take its clock from the recording. Each --expect is a
line the recording must have, --seeds the number of seeds it must declare, and --save needs the runs to write a save. Exits 0 when everything holds; 1 when it
doesn't; a run's own status when it fails, so 66, the program's status without the ROM, skips the test.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile


def run(command: list[str], run_dir: str, save_dir: str) -> tuple[int, str]:
    os.makedirs(save_dir)
    done = subprocess.run(command + ["--run-dir", run_dir, "--save-dir", save_dir], capture_output=True, text=True)
    # The program's own lines start with "port: ", after the time and process that SDL_Log puts first on some hosts
    log = (done.stderr + done.stdout).splitlines()
    lines = [line[line.index("port: "):].strip() for line in log if "port: " in line]
    if done.returncode != 0:
        for line in lines[-5:]:
            print(f"  {line}")
    return done.returncode, lines[-1] if lines else ""


def read(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def saves(directory: str) -> dict[str, bytes]:
    """The save files a run left, by name, without a temporary file of an unfinished write"""
    return {name: read(os.path.join(directory, name)) for name in sorted(os.listdir(directory))
            if not name.endswith(".tmp")}


def run_txt(run_dir: str) -> dict[str, str]:
    """run.txt's lines that a replay must repeat: the clock's time and the seeds the game used"""
    values = {}
    for line in read(os.path.join(run_dir, "run.txt")).decode().splitlines():
        key, _, value = line.partition(": ")
        if key in ("time", "status", "completed", "seeds") or key.startswith("seed_"):
            values[key] = value
    return values


def recording_lines(path: str) -> list[str]:
    """A recording's lines without its comments: what a replay of it reads"""
    lines = []
    for line in read(path).decode().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            lines.append(line)
    return lines


def with_header(source: str, header: list[str], target: str) -> None:
    """source with header's lines after its first line, "pbwinput 1", before any frame line"""
    lines = read(source).decode().splitlines(keepends=True)
    with open(target, "w", newline="\n") as f:
        f.write(lines[0])
        for line in header:
            f.write(line + "\n")
        f.writelines(lines[1:])


def compare(what: str, first: object, second: object) -> bool:
    if first == second:
        return True
    print(f"FAIL: {what} differ between the recorded run and its replay")
    return False


def compare_frames(first: str, second: str) -> bool:
    a = read(os.path.join(first, "frames.tsv")).decode().splitlines()
    b = read(os.path.join(second, "frames.tsv")).decode().splitlines()
    for number, (x, y) in enumerate(zip(a, b), 1):
        if x != y:
            print(f"FAIL: frames.tsv line {number} differs:\n  recorded {x}\n  replayed {y}")
            return False
    if len(a) != len(b):
        print(f"FAIL: frames.tsv has {len(a)} lines in the recorded run, {len(b)} in its replay")
        return False
    print(f"frames.tsv: the same {len(a) - 2} frames in both runs")
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", help="the input file the first run plays")
    parser.add_argument("--header", action="append", default=[], help="a line to add to the input file's header")
    parser.add_argument("--fixed-time", help="--fixed-time for the first run only")
    parser.add_argument("--expect", action="append", default=[], help="a line the recording must have")
    parser.add_argument("--seeds", type=int, help="how many seeds the recording must declare")
    parser.add_argument("--save", action="store_true", help="the runs must write a save")
    parser.add_argument("command", nargs="+", help="the program and its arguments, after --")
    args = parser.parse_args(argv)

    with tempfile.TemporaryDirectory(prefix="record-") as work:
        recorded = args.command[:]
        if args.input is not None:
            source = args.input
            if args.header:
                source = os.path.join(work, "input.pbwinput")
                with_header(args.input, args.header, source)
            recorded += ["--input", source]
        if args.fixed_time is not None:
            recorded += ["--fixed-time", args.fixed_time]
        recording = os.path.join(work, "recording.pbwinput")
        again = os.path.join(work, "again.pbwinput")
        runs = []
        for name, command in (("recorded", recorded + ["--record", recording]),
                              ("replayed", args.command + ["--input", recording, "--record", again])):
            run_dir = os.path.join(work, name)
            status, message = run(command, run_dir, os.path.join(work, name + "-saves"))
            print(f"{name}: status {status}: {message}")
            if status != 0:
                return status
            runs.append(run_dir)

        lines = recording_lines(recording)
        print(f"the recording: {len(lines)} lines, " +
              ", ".join(line for line in lines if not line[0].isdigit() and not line.startswith("seed")) +
              f", {sum(line.startswith('seed') for line in lines)} seeds, " +
              f"{sum(line[0].isdigit() for line in lines)} frame lines")
        failed = False
        for line in ["frames video"] + args.expect:
            if line not in lines:
                print(f"FAIL: the recording has no line \"{line}\"")
                failed = True
        seeds = sum(line.startswith("seed ") for line in lines)
        if args.seeds is not None and seeds != args.seeds:
            print(f"FAIL: the recording declares {seeds} seeds, not {args.seeds}")
            failed = True
        first, second = runs
        failed |= not compare_frames(first, second)
        if os.path.exists(os.path.join(first, "memory.txt")):
            if compare("memory.txt's dumps", read(os.path.join(first, "memory.txt")),
                       read(os.path.join(second, "memory.txt"))):
                print("memory.txt: the same dumps in both runs")
            else:
                failed = True
        values = run_txt(first)
        if compare("run.txt's time and seeds", values, run_txt(second)):
            print(f"run.txt: time {values.get('time')}, {values.get('seeds')} seeds, the same in both runs")
        else:
            print(f"  recorded {values}\n  replayed {run_txt(second)}")
            failed = True
        first_saves = saves(first + "-saves")
        if args.save and not first_saves:
            print("FAIL: the runs wrote no save")
            failed = True
        if compare("the save files", first_saves, saves(second + "-saves")):
            print(f"saves: {', '.join(first_saves) or 'none'}, the same bytes in both runs")
        else:
            failed = True
        if compare("the recordings of the run and of its replay", lines, recording_lines(again)):
            print("the replay's own recording says what the run's does")
        else:
            failed = True
        return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
