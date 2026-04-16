#!/usr/bin/env python3
"""
Multithreaded Dr. Mario tournament runner.

Usage:
    python run_tournament.py <bot1_name> <bot2_name> [num_trials]

Exit codes from ./drmario:
    RC=1 → bot1 wins
    RC=2 → bot2 wins
"""

import atexit
import os
import random
import signal
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

LOCK_FILE = "/tmp/drmario_tournament.lock"

# Global shutdown flag and executor reference for signal handling
shutdown_event = threading.Event()
_executor = None


def _handle_sigint(signum, frame):
    """Catch CTRL+C: signal all threads and child processes to stop."""
    sig = signal.Signals(signum).name
    print(f"\n{sig} received — shutting down gracefully...", flush=True)
    shutdown_event.set()
    if _executor is not None:
        _executor.shutdown(wait=False, cancel_futures=True)


def acquire_lock():
    """Prevent multiple instances via a PID file. Exits fast if already running."""
    if os.path.exists(LOCK_FILE):
        try:
            pid = int(open(LOCK_FILE).read().strip())
            # Check if the process is actually still alive
            os.kill(pid, 0)
            print(
                f"Another tournament instance is already running (PID {pid}). Exiting.",
                file=sys.stderr,
            )
            sys.exit(1)
        except (ValueError, ProcessLookupError):
            # Stale lock file — clean it up
            os.remove(LOCK_FILE)
        except PermissionError:
            # Process exists but we can't signal it — treat as running
            print(
                f"Another tournament instance may be running (PID {pid}). Exiting. WARNING DANGER: DO NOT KILL THE TOURNAMENT PROGRESS, INSTEAD WAIT A BIT AND TRY AGAIN",
                file=sys.stderr,
            )
            sys.exit(1)

    with open(LOCK_FILE, "w") as f:
        f.write(str(os.getpid()))
    atexit.register(release_lock)


def release_lock():
    """Remove the lock file on clean exit."""
    try:
        os.remove(LOCK_FILE)
    except OSError:
        pass


def run_match(bot1: str, bot2: str, match_num: int) -> int:
    """Run a single drmario match and return the winner (1 or 2)."""
    if shutdown_event.is_set():
        return -1

    flipped = bool(random.random() > 0.5)

    if flipped:
        cmd = f"./drmario --bot1 {bot2} --bot2 {bot1}"
    else:
        cmd = f"./drmario --bot1 {bot1} --bot2 {bot2}"
    proc = subprocess.Popen(
        cmd,
        shell=True,
        executable="/bin/bash",
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    # Poll so we can react to shutdown while waiting
    start_time = time.monotonic()
    timeout_seconds = 120  # 2 minutes
    while proc.poll() is None:
        if shutdown_event.is_set():
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            return -1
        # Check if the match has exceeded the timeout
        elapsed = time.monotonic() - start_time
        if elapsed >= timeout_seconds:
            print(
                f"[Match {match_num}] Timed out after {int(elapsed)}s, killing process.",
                file=sys.stderr,
            )
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            return 3  # treat as error/unexpected
        # Brief sleep to avoid busy-waiting
        threading.Event().wait(0.1)

    rc = proc.returncode
    if rc not in (0, 1, 2):
        print(
            f"[Match {match_num}] Unexpected return code {rc}, skipping.",
            file=sys.stderr,
        )
        return 3

    if flipped:
        if rc == 1:
            return 2
        if rc == 2:
            return 1
    return rc


def main():
    # Register CTRL+C handler
    signal.signal(signal.SIGINT, _handle_sigint)

    acquire_lock()

    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <bot1_name> <bot2_name> [num_trials]")
        sys.exit(1)

    bot1 = sys.argv[1]
    bot2 = sys.argv[2]
    trials = int(sys.argv[3]) if len(sys.argv) > 3 else 300

    bot1_wins = 0
    bot2_wins = 0
    ties = 0
    errors = 0
    lock = threading.Lock()

    cpu_count = os.cpu_count() or 8

    max_workers = min(trials, cpu_count/3)
    print(f"Tournament: {bot1} vs {bot2} — {trials} trials")
    print(f"Running... {max_workers} threads")

    global _executor
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        _executor = executor
        futures = {
            executor.submit(run_match, bot1, bot2, i + 1): i + 1 for i in range(trials)
        }

        for future in as_completed(futures):
            if shutdown_event.is_set():
                break

            match_num = futures[future]
            try:
                winner = future.result()
            except Exception as e:
                with lock:
                    errors += 1
                print(f"[Match {match_num}] Exception: {e}", file=sys.stderr)
                continue

            if winner == -1:
                continue

            with lock:
                if winner == 1:
                    bot1_wins += 1
                elif winner == 2:
                    bot2_wins += 1
                else:
                    ties += 1

            completed = bot1_wins + bot2_wins + ties
            if completed % 10 == 0:
                print(
                    f"  Progress: {completed}/{trials} matches completed...", flush=True
                )

    if shutdown_event.is_set():
        print("\nTournament cancelled by user.", flush=True)
        release_lock()
        sys.exit(130)  # 128 + SIGINT(2)

    # ── Final Results ──────────────────────────────────────────────
    total = bot1_wins + bot2_wins + ties
    print("\n" + "=" * 50)
    print("          TOURNAMENT RESULTS")
    print("=" * 50)
    print(f"  Bot 1 ({bot1}):  {bot1_wins:>5} wins")
    print(f"  Bot 2 ({bot2}):  {bot2_wins:>5} wins")
    if ties:
        print(f"  Ties:         {ties:>5}")
    if errors:
        print(f"  Errors:         {errors:>5}")
    print("-" * 50)
    if total > 0:
        print(f"  {bot1} win rate: {bot1_wins / total * 100:.1f}%")
        print(f"  {bot2} win rate: {bot2_wins / total * 100:.1f}%")
    print("=" * 50)


if __name__ == "__main__":
    main()
