# Joining a group needs the creator's approval

Since 2026-11-24 (Weili: "anyone, not for this group, need approval by the
group admin"), a group no longer accepts every friend request. On all groups:

| who asks | what happens |
|---|---|
| a stranger | held in `join_request_table`, the creator is told, nothing else happens until the creator answers |
| the first member of an empty group | accepted as before and becomes the creator (remembered in `service_text` `join.first_member`, so a second stranger arriving before the first one connects is held) |
| an existing member (in `member_table`) | accepted as before |
| a registered agent (`/agent add`, `:5000/agent/add`) | accepted as before; adding an agent is still direct |
| a blocked user | refused as before, nothing stored |
| someone rejected in the last 7 days | ignored silently (their client keeps re-sending the request) |

Existing Carrier friends never send a request, so existing members and anyone
the group already befriended are unaffected. A member removed with `/del` must
be approved to come back.

Requests parked on Express arrive through the same callback and are held the
same way. Held requests survive a restart. At most 200 are held per group;
more are dropped (logged). The name and hello are stored cut to 512 bytes.

## What the creator sees

When a request arrives, the creator alone gets:

    Join request #12 from rio: "hi, I'd like to join"
    Type /approve 12 to let them in, /reject 12 to drop it. /pending lists all.

Each request is announced once (toxcore re-sends requests; a repeat only
refreshes the stored name and hello). If the creator was offline, they get
`3 join requests are waiting for you. Type /pending.` when they next connect.

| command (creator only) | effect |
|---|---|
| `/pending [page]` | requests waiting, 10 per page |
| `/approve <n>` | accepts the friend request; the person becomes a member when they next come online |
| `/reject <n>` | drops it; their requests are ignored for 7 days |

Anyone else gets "/approve is for the group's creator. Type /help." Every reply
stays under 1 KB (worst-case `/pending` page 965 bytes, notice 459 bytes).

Journal: `join held #n from=<userid> …`, `join accepted (first member|already a
member|registered agent)`, `join refused (blocked)`, `join ignored (rejected
within 7 days)`, `join approved|rejected #n … by creator`, `join dropped`.

Code: `Tools/JoinRequests.h` (text), `DatabaseProxy` `join_request_table`,
`CarrierRobot::onJoinRequest` / `approveCmd` / `rejectCmd` / `pendingCmd`.
Tests: `linux/tools/test_join_requests.cpp`:

    g++ -std=c++14 -I linux/service/source/Tools -o test_join_requests linux/tools/test_join_requests.cpp
