# Bot Creation Prompt

**Goal:** Build the best Dr. Mario bot. Give it a fun 4–5 letter name.

**Where to start:** Look at existing bots in `bot/`. You're welcome to copy the best one and improve it.

## Testing

1. Run `make` to compile.
2. Compare your bot against the current best:

   ```bash
   python3 run_tournament.py <YOUR_BOT_NAME> <BEST_BOT_NAME> 30 #NOTE the game actually runs 10 trials so this is 10 x 30 = 300 real-trials.
   ```

   - Run exactly **30 trials**, only once per iteration.
   - Set timeout to **300–600 seconds**.
   - Ignore improvements under **3%** — that's just variance (roughly ±5%).

## Collaboration Rules

- Other workers may be adding bots concurrently. If `make` fails, wait and retry — **do not delete other bots' files**.
- If `run_tournament.py` fails because a tournament is already running, sleep 60 and retry until it succeeds.

## Current Best Bot

- **BEST_BOT_NAME** = `fever`
