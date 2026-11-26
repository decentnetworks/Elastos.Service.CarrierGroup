# Session memory (read me)

Last updated: 2026-11-26

## Current pins
- Codex owns CarrierGroup; decent-hq coordinates and reviews work. Read `docs/HANDOFF.md` before operational work.
- Edit, commit and push in `/Users/weili/devs/agentnet-v2/group`, branch `feat/agentnet-proto-v2`. Build, test and deploy on snoopy in `~/devs/agentnet-v2-group` after pulling.
- Do not use the StudioProjects master checkout for this work.

## Do / Don’t
- Check the Codex mesh inbox before each step and while waiting on decent-hq.
- Start implementation only when decent-hq or Weili assigns a task; handoff open items are not tasks.
- Test only on stage and group 8. Back up before deployment; verify 11 processes, counts, join modes and crash-free journal afterwards. Report changes to decent-hq with commit, tests and deployment checks.
- Keep keys, user IDs and addresses out of public issues.

## Open follow-ups
- **Deferred request from Weili (2026-11-25):** provide a way to check whether a supplied user ID belongs to a group. Current `/list` shows names only and there is no read-only chat command for exact user ID membership lookup. Discuss desired behavior, permissions, output and command syntax later. No implementation is authorized by this request.

- **Group avatar issue reported by Weili (2026-11-26):** gpu-59 has an avatar in the friend list/direct chat but a fallback icon in decent-agents. The Codex mesh received the matching group messages. At the handoff source revision, `buildOutboundMessageForRecipient` sends ordinary human text recipients only name, body and timestamp; `origin.userid` and `origin.agentnet.avatar_url` are in the registered-agent envelope. This is a confirmed metadata gap; the exact client fallback and end-to-end cause remain unverified. Coordinate client/protocol changes with decent-hq before implementing. No runtime changes made.
- Codex mesh profile: name configured as `Codex`; no avatar configured in the inspected profile/daemon setup.
- Mac pushes to decentnetworks are denied. HQ directed documentation patch transport to snoopy via `git format-patch`, `scp`, `git am`, then push there. Original membership-request docs are published as `63d9b7e`.
