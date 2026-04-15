#!/usr/bin/env python3
"""
Multithreaded Dr. Mario tournament runner.

Usage:
    python run_tournament.py <bot1_name> <bot2_name> [num_trials]

Exit codes from ./drmario:
    RC=1 → bot1 wins
    RC=2 → bot2 wins
"""

import random
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed


def run_match(bot1: str, bot2: str, match_num: int) -> int:
    """Run a single drmario match and return the winner (1 or 2)."""

    flipped = bool(random.random() > 0.5)

    if flipped:
        cmd = f"./drmario --bot1 {bot2} --bot2 {bot1}"
    else:
        cmd = f"./drmario --bot1 {bot1} --bot2 {bot2}"
    result = subprocess.run(
        cmd,
        shell=True,
        executable="/bin/bash",
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    rc = result.returncode
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
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <bot1_name> <bot2_name> [num_trials]")
        sys.exit(1)

    bot1 = sys.argv[1]
    bot2 = sys.argv[2]
    trials = int(sys.argv[3]) if len(sys.argv) > 3 else 300

    print(f"Tournament: {bot1} vs {bot2} — {trials} trials")
    print(f"Running... (using up to {min(trials, 32)} threads)")

    bot1_wins = 0
    bot2_wins = 0
    ties = 0
    errors = 0
    lock = threading.Lock()

    max_workers = min(trials, 32)

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = {
            executor.submit(run_match, bot1, bot2, i + 1): i + 1 for i in range(trials)
        }

        for future in as_completed(futures):
            match_num = futures[future]
            try:
                winner = future.result()
            except Exception as e:
                with lock:
                    errors += 1
                print(f"[Match {match_num}] Exception: {e}", file=sys.stderr)
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
