# CarrierGroup server — handoff (2026-11-25)

The group service now passes to Codex; decent-hq keeps the Beagle clients and
the protocol. This page is the starting point; details live in the docs it
links. Facts below were checked on snoopy on 2026-11-25.

## What runs where

- **Box:** snoopy, `ssh beagle@10.0.0.224` (DEPLOY.md still names the older
  address, 10.0.0.115). It runs one systemd user unit, `carriergroup.service`.
  - `WorkingDirectory` is `~/devs/Elastos.Service.CarrierGroup/linux/ui/appserver`.
  - `ExecStart` is `startCarrierByServer.sh 0.0.0.0 5000 …/linux/ui/runtime_data`.
  - It runs the Flask appserver (`server.py`, `:5000`) plus one `CarrierService`
    process per group (11 groups). Each group's data is in
    `runtime_data/carrierService<N>/`: `chatrobot.db` (service state) and
    `carrier.data` (Carrier identity **and private key**: never copy, print or
    export it).
- **Source:** `~/devs/agentnet-v2-group`.
  - Branch `agentnet-proto-v2` → origin
    `decentnetworks/Elastos.Service.CarrierGroup` `feat/agentnet-proto-v2`,
    pushed over ssh.
  - The live directory `~/devs/Elastos.Service.CarrierGroup` is still git
    `master` 7c0f763. **Never `git checkout` there**; only built artifacts are
    swapped in (see DEPLOY.md).
- **Native SDK:** libraries come from `~/devs/agentnet-v2-native-bounded/dist/lib`.
  - That is branch `fix/bounded-peer-strings` 66e45ed (= e8eb0b0 + bounded
    peer string copies), in `decentnetworks/Elastos.NET.Carrier.Native.SDK`.
    It is not merged anywhere.
  - The drop-in `~/.config/systemd/user/carriergroup.service.d/override.conf`
    sets `CARRIER_LIB_DIR` to that prefix. The service is built in
    `build/service-e8` against e8eb0b0 headers; the ABI is the same.
- **Live binary (2026-11-25 00:32 PST):** md5 `b1700f33cadb1d75a0b82e3d0dcc99d0`,
  commit 7b6aa1b.

## Build, deploy, roll back

Full detail is in [DEPLOY.md](DEPLOY.md). The short version:

```bash
cd ~/devs/agentnet-v2-group && git fetch && git status -sb
cmake --build build/service-e8 -j4                      # service only
A=~/devs/Elastos.Service.CarrierGroup/linux/ui/appserver
cp -p $A/carrierService $A/carrierService.bak-$(date +%Y%m%d)<letter>-<prev-commit>
cp build/service-e8/carrierService $A/carrierService.new
chmod 755 $A/carrierService.new && mv -f $A/carrierService.new $A/carrierService   # never cp over it
systemctl --user restart carriergroup.service           # takes every group offline ~30 s
```

**Check after every restart:**
- `systemctl --user is-active carriergroup.service`;
- `pgrep -fc "^CarrierService 127"` is 11;
- the journal since the restart has 11 × `OnCarrierConnection status: 0`, and
  no `overflow|terminated|segfault`;
- `curl -s localhost:5000/groups` member counts match before;
- each group's `service_text` `join.mode` is unchanged.

**Rollback:** `mv` the newest `carrierService.bak-*` back into place, then
restart. Backups are named `.bak-<date><letter>-<commit it replaced>`.

**SDK rollback:** point `CARRIER_LIB_DIR` back at
`agentnet-v2-native-e8eb0b0/dist/lib`; the old drop-in is in
`~/cg-e2e/override.conf.bak-20261124-e8eb0b0`. Then
`systemctl --user daemon-reload` and restart. Never reinstall a `dist/lib`
that running groups have mapped: build a new prefix instead.

## Tests

### Unit tests

Pure logic, no network:

```bash
cd ~/devs/agentnet-v2-group
for t in test_floodguard test_command_text test_member_list test_join_requests test_member_target; do
  g++ -std=c++14 -I linux/service/source/Tools -I linux/common -o /tmp/$t linux/tools/$t.cpp \
      $( [ $t = test_floodguard ] && echo linux/service/source/Tools/FloodGuard.cpp ) && /tmp/$t
done
```

`test_floodguard` can also replay `~/cg-e2e/floodguard/gojibot-1124.tsv`
(GojiBot's real burst; pass it as the argument). The SDK's unit test for
over-long peer strings is `tests/unit/peerstr_test.c` in the SDK branch above
(see its commit message for how it was built).

### Stage group

A private group service on snoopy, never a real group. Its setup is in
`~/cg-e2e/stage`:
- `start.sh` runs a second appserver on port 5099, manager port 3399, data in
  `stage/rt/`;
- the group's userid is in `stage/groupid.txt`; its address is
  `4ATaQv5X1JMzzRPb1tmweRfNAPBC9pK5Z6agmSSnRbMoWYAZPfRx`.

To run a test:
1. Copy the new binary to `stage/appserver/carrierService`.
2. Start it with
   `setsid nohup stage/start.sh > stage/server.log 2>&1 < /dev/null &`.
3. Stop it by killing the PIDs that match `port 5099|3399 /home/beagle/cg-e2e`.
   Kill explicit PIDs: `pgrep -f` loops have killed the ssh shell.

**Test identities** (throwaway, in `~/cg-e2e/id<X>`):

| identity | userid starts | stage group | live group 8 |
|---|---|---|---|
| e2e-D | `EHNocbXs` | creator | member |
| e2e-C | `E1tkwT93` | member | member |
| e2e-A | `BMm4Yz3Q` | member | member |
| e2e-B | `7UNFCSSc` | blocked | **creator** |
| e2e-E, e2e-F, e2e-G, e2e-H | | strangers or ex-members | |

**Clients:**
- `cg_e2e` is `linux/tools/cg_e2e.c`: join, send, listen. Run it with no
  arguments for usage.
- `cg_burst` is `linux/tools/cg_burst.c`: cg_e2e plus `-i <ms>` between sends
  and up to 256 sends.
- Example scenarios are in `linux/tools/stage-tests/`. They are the scripts
  used for #21, #22, #26, join approval, member targets and names; they
  assume `~/cg-e2e`.

**Live checks:** use only **group 8**, a test group whose creator is e2e-B, or
the stage group. Never real groups.

**Known noise:**
- The stage group's connection drops now and then. The sender log shows
  `RECEIPT … state=2 (error)`, and those messages never arrive. Resend them.
- Allow about 25 s for a client to connect; use `-t 90` or more.

## What the group does (docs)

- [GROUP-COMMANDS.md](GROUP-COMMANDS.md): slash commands are never relayed;
  `/list` pages; `/help`; and `/del` `/block` `/unblock` by number, userid,
  name or beagles.eth name.
- [GROUP-JOIN.md](GROUP-JOIN.md): `/join open|approve` per group;
  `/pending`, `/approve`, `/reject`. Group 11 (decent-agents) is `approve`;
  groups 1–10 are open.
- [ANTI-FLOOD.md](ANTI-FLOOD.md): rate limits, duplicates, mute, block,
  `/unmute`.
- [GROUP-ENS.md](GROUP-ENS.md): the group signs its own beagles.eth record
  (`/ens`, `/profile`), and its signer list (`/group/signers`). The group key
  signs only bytes the service builds itself; there is no sign-anything
  endpoint.
- [DEPLOY.md](DEPLOY.md): build, stage and swap, library path.
- Agent and file protocols: `AGENT_*_PROTOCOL.md`, `GROUP_FILE_MESSAGE.md`.

**Member names:** since 7b6aa1b, a member's stored name changes only when a
fresh userinfo packet arrives (`OnCarrierFriendInfoChanged`). Connection
events and restarts keep it. The journal logs
`member name <id>: old -> new (userinfo|connect)`.

## Open items

1. **Sub-second reconnects** from some peers. Median connection to the group,
   measured 2026-11-24 15:33 to 00:30:
   - gpu-59 `Gg89Kii1`: 0.2 s;
   - ai-btc-trader `7xLPH2kj`: 0.7 s;
   - GojiBot `8mtSvgSN`: 1.0 s;
   - Noor `CoHMW66W`: 1.4 s;
   - Wei Li `CEDADaws`: 2.9 s;
   - mac-dev `AH7Fv8Tn`: 43 s; other members typically minutes.

   The group code is the same for all of them, so this is peer side
   (JS peer, or two processes on one identity). Not resolved. gpu-59 also
   re-sends each message about 13× at ~1.5 s; the flood guard drops the copies.
2. **Names lost before 7b6aa1b.** 394 of 508 members of group 1 (2 in group 2)
   are named `CarrierUser-xxxxxxxx`. They recover as each member's next
   userinfo arrives. Nothing in the server holds the old names.
3. **The SDK does not persist friend userinfo changes.** `carrier.data` is
   written only on friend add or remove. Fix in the SDK: call
   `store_persistence_data` from `notify_friend_description_cb` when changed,
   debounced. Clients on this SDK have the same stale cache.
4. **Held join requests are let in only on `/join open`.** If a group's mode
   is changed to open some other way (DB edit), requests already held stay in
   `join_request_table` until the creator sends `/join open`. All groups had
   0 held on 2026-11-25.
5. **Member counts:** no oddity verified. The `:5000/groups` count is the
   `member_table` row count; it includes agents and members who never came
   back. Group 8 has 8 members after test cleanup.
6. **SQL built by string concatenation** remains in the manager
   (`libcarrierManager.so` `group_table` UPDATE) and in some old
   `DatabaseProxy` paths (`addBlockMember`, `removeBlockMember`). Values there
   are userids or the group name. `updateMemberInfo` was fixed in 7b6aa1b.
7. `zz-test-g10.beagles.eth` (a test record for group 10) is still on the
   gateway; there is no delete endpoint.
8. Relay delivery under load: during a 174-message stage burst, a listener
   whose own connection flapped missed 20 forwarded messages. They are stored
   in `message_table`, not dropped by the guard. Not investigated.
9. SDK branch `fix/bounded-peer-strings` is not merged into the SDK's main
   lines. The iOS, Android and Desktop clients built on e8eb0b0 still have the
   over-long-profile crash.
