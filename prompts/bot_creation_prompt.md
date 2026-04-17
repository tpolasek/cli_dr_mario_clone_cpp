# Bot Creation Prompt

**Goal:** Build the best Dr. Mario bot. Give it a fun 4–5 letter name.

**Where to start:** Look at existing bots in `bot/`.

## Testing

1. Run `make` to compile.
2. Compare your bot against the current best:

   ```bash
   python3 run_tournament.py <YOUR_BOT_NAME> <BEST_BOT_NAME> 16 #NOTE the drmario binary actually runs 10 trials internally so this is 16 x 10 = 160 real-trials.
   ```
   - Set timeout to **600 seconds**.
   - Ignore improvements under **10%** — the variance may be even larger

## Collaboration Rules

- Other workers may be adding bots concurrently. If `make` fails, wait and retry — **do not delete other bots' files**.
- If `run_tournament.py` fails because a tournament is already running, sleep 60 and retry until it succeeds.

## Current Best Bot
Analyze the best bot, you may choose to copy the strategy and improve on it as you so desire.
- **BEST_BOT_NAME** = `lucky`
