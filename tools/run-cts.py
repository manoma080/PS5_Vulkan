#!/usr/bin/env python3
# PS5 Vulkan - run Vulkan CTS cases on the console (PPSA99015).
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a selection of the Vulkan CTS on the console and summarise it.

The CTS title (tools/build-cts-title.sh) reads its arguments from
/app0/cts/args.txt. This writes that file for a case pattern (--case) or a
case list (--caselist, uploaded as /app0/cts/caselist.txt), with the standard
flags (the log beside the title, images and shader sources not logged, the
log not flushed after every write: on the console that made a 322-message case
take 11.5 s instead of 17 ms), runs
the title through tools/run-title.py, and reads the per-case lines the
platform mirrors to klog: a count by status, every case that failed, and the
case that was running when the title crashed. The QPA log is fetched as well.

Exit status: 0 when the run ended with no failure, 1 when cases failed, 3 when
the run did not end (a crash or a hang).
"""

import argparse
import collections
import io
import re
import sys
from ftplib import FTP
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ps5_console  # noqa: E402
import importlib.util  # noqa: E402

_spec = importlib.util.spec_from_file_location("run_title", Path(__file__).resolve().parent / "run-title.py")
run_title_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(run_title_module)

TITLE = "PPSA99015"
CASE = re.compile(r"\[cts\] Test case '([^']+)'\.\.")
STATUS = re.compile(r"\[cts\]\s+(Pass|Fail|NotSupported|QualityWarning|CompatibilityWarning|ResourceError|"
                    r"InternalError|Crash|Timeout|Waiver)\b\s*(.*)")


def upload(settings, files):
    with FTP() as ftp:
        ftp.connect(settings["host"], settings["ftp_port"], timeout=30)
        ftp.login(settings["ftp_user"], settings["ftp_password"] or "codex")
        for name, text in files.items():
            ftp.storbinary(f"STOR /data/homebrew/{TITLE}/cts/{name}", io.BytesIO(text.encode()))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--case", help="a case pattern, as --deqp-case takes it")
    selection.add_argument("--caselist", help="a file of case names, one a line")
    parser.add_argument("--timeout", type=float, default=14400.0, help="watchdog for the whole run, seconds")
    parser.add_argument("--stall", type=float, default=90.0,
                        help="seconds of silence (klog quiet, the log not growing) that end the run")
    parser.add_argument("--elf", default="build/cts/llvm-pie.elf", help="the linked ELF, to symbolise a crash")
    parser.add_argument("--extra", action="append", default=[], help="another deqp argument")
    parser.add_argument("--log-flush", choices=("enable", "disable"), default="disable",
                        help="flush the log after every write (--deqp-log-flush); each flush to the "
                             "console's storage costs milliseconds, so it is off unless asked for")
    parser.add_argument("--verbose", action="store_true", help="print every case as it ends")
    args = parser.parse_args()

    settings = ps5_console.load_settings()
    flags = [
        "--deqp-log-filename=/app0/cts/TestResults.qpa",
        "--deqp-archive-dir=/app0/cts",
        "--deqp-log-images=disable",
        "--deqp-log-shader-sources=disable",
        "--deqp-shadercache=disable",
        f"--deqp-log-flush={args.log_flush}",
    ]
    files = {}
    if args.case:
        flags.append(f"--deqp-case={args.case}")
    else:
        files["caselist.txt"] = Path(args.caselist).read_text()
        flags.append("--deqp-caselist-file=/app0/cts/caselist.txt")
    flags += args.extra
    files["args.txt"] = "\n".join(flags) + "\n"
    upload(settings, files)

    counts = collections.Counter()
    failures = []
    state = {"case": None, "status_seen": True}

    def on_line(line):
        match = CASE.search(line)
        if match:
            state["case"] = match.group(1)
            state["status_seen"] = False
            return
        match = STATUS.search(line)
        if match and state["case"] and not state["status_seen"]:
            status, detail = match.group(1), match.group(2).strip()
            counts[status] += 1
            state["status_seen"] = True
            if status not in ("Pass", "NotSupported"):
                failures.append((state["case"], status, detail))
            if args.verbose or status not in ("Pass", "NotSupported"):
                print(f"  {status:<14} {state['case']}  {detail}", flush=True)

    # klog can lag or drop lines under load; the log the CTS writes on the
    # console is the other sign of progress.
    qpa_size = {"bytes": -1}

    def progressing():
        with FTP() as ftp:
            ftp.connect(settings["host"], settings["ftp_port"], timeout=30)
            ftp.login(settings["ftp_user"], settings["ftp_password"] or "codex")
            ftp.voidcmd("TYPE I")
            size = ftp.size(f"/data/homebrew/{TITLE}/cts/TestResults.qpa") or 0
        grew = size > qpa_size["bytes"]
        qpa_size["bytes"] = size
        return grew

    ended, output, _ = run_title_module.run_title(
        TITLE, r"\[cts\] run ends", args.timeout, fetch=["cts/TestResults.qpa"],
        elf=args.elf, on_line=on_line, stall=args.stall, progressing=progressing)
    # The log the CTS wrote is the record: klog can drop lines under load.
    qpa = output.with_name(output.stem + "-cts_TestResults.qpa")
    text = qpa.read_text(errors="replace") if qpa.exists() else ""
    results = re.findall(r"#beginTestCaseResult (\S+)(.*?)(?=#beginTestCaseResult|\Z)", text, re.S)
    counts = collections.Counter()
    unfinished = None
    for name, body in results:
        status = re.search(r'StatusCode="(\w+)"', body)
        if "#endTestCaseResult" not in body or not status:
            unfinished = name
            continue
        counts[status.group(1)] += 1
        if status.group(1) not in ("Pass", "NotSupported"):
            detail = re.search(r'StatusCode="\w+">([^<]*)<', body)
            print(f"  {status.group(1):<14} {name}  {detail.group(1) if detail else ''}")
    print(f"cases: {sum(counts.values())}  " + "  ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    if ended != "finished":
        print(f"the run {ended}" + (f" in {unfinished}" if unfinished else ""))
        return 3
    return 1 if any(k not in ("Pass", "NotSupported") for k in counts) else 0


if __name__ == "__main__":
    sys.exit(main())
