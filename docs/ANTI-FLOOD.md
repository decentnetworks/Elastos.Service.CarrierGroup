# Anti-flood in the group service (feedback#21)

Since 2026-11-24 every group checks each member message before it is stored
and forwarded. Code: `linux/service/source/Tools/FloodGuard.{h,cpp}` (pure
logic, unit-tested) and `CarrierRobot::floodAllows` (the glue). Commands are
never checked; file messages get the rate limits only.

## Rules (per group, per member)

1. **Rate limits.** Not forwarded over any of: 2 / second, 20 / minute,
   120 / hour, 600 / day, 3,000 / week, 10,000 / month (30 days). Only forwarded
   messages count. The sender gets one notice per window, with the wait.
   After a rate trip nothing more from that member is forwarded **until they have
   sent nothing for 10 minutes** (every attempt while held restarts the 10 minutes):
   a burst is not forwarded, it is not just slowed down.
2. **Duplicates** (texts of 10+ characters; the trailing `[YYYY-MM-DD HH:MM:SS]`
   marker is stripped first). Not forwarded if it repeats:
   - the same text from the same member within 10 min,
   - the same text from anyone within 1 min,
   - a similar text (identical once digits and whitespace are removed) from the
     same member within 5 min.
   One notice per 10 min.
3. **Spammers.** 3 violations within 1 h → muted 24 h (told once, reminded at
   most hourly). 3 mutes within 7 days → blocked: added to `block_member_table`,
   removed from the member list and the friend list, so the group refuses them if
   they ask to join again. The creator and signers are muted like anyone but are
   never blocked automatically.

A **violation is one flood episode**, not one dropped message: it starts with a
drop, ends after 10 min without drops, and counts again for every further 10 min
it lasts. A bot flushing a backlog for 3 minutes is one violation (dropped, not
muted); someone flooding nonstop is muted after ~20 minutes; three separate
floods within an hour mute. (`FloodConfig::CountMode::PerDrop` counts every drop.)

Measured on GojiBot's real 2026-11-24 traffic (174 queued reports flushed
09:53–09:56 UTC, then 12 hourly reports from 10:09): 5 of the 174 forwarded,
12/12 hourly reports forwarded, 1 notice, no mute. Counting every drop instead
(literal): 15 of 174 forwarded, then muted 24 h, 0/12 hourly forwarded. Without
the hold: 51 of 174 forwarded (20/min allows that much).

## State

- Rate counters and the duplicate memory are rebuilt at startup from
  `message_table` (last 30 days of forwarded messages; the journal logs
  `flood guard seeded: N messages, M mutes`).
- Mutes are stored in `flood_mute_table` (Friendid, MutedAt, MutedUntil, ms) and
  re-read at startup, so a restart does not lift a mute or reset the 7-day count.

## Creator commands

| command | effect |
|---|---|
| `/unmute <userid>` | end a mute now and forget the member's mutes and violations |
| `/unblock <userid>` | remove a block (automatic or `/block`), clear mutes; they can join again |

## Log lines (journal)

`flood drop from=<userid> len=<n> reason=<why>`, `flood mute from=<userid> until_ms=<t>`,
`flood block from=<userid> (muted 3 times in 7 days)`, `flood unmute|unblock <userid> by creator`.

## Tests

`linux/tools/test_floodguard.cpp` (73 checks incl. the GojiBot replay):

    g++ -std=c++14 -I linux/service/source/Tools -I linux/common \
        -o test_floodguard linux/tools/test_floodguard.cpp linux/service/source/Tools/FloodGuard.cpp
    ./test_floodguard <gojibot-1124.tsv>

The fixture (`<send time us>\t<JSON text>` per line) is not in the repo; it is
on snoopy at `~/cg-e2e/floodguard/gojibot-1124.tsv`.
