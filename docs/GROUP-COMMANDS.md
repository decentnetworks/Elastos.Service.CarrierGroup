# Slash commands are never relayed (feedback#22)

Since 2026-11-24, a group message is a **command** when its text, after
leading whitespace, starts with `/` followed by an ASCII letter (`/help`,
`/friends list`). A command is never stored in `message_table` and never
relayed to members, whether or not the group knows it.

| message | what happens |
|---|---|
| a known command (`/help`, `/list`, `/ens show`, …) | runs as before; any reply goes to the sender only |
| an unknown command (`/friends list`) | sender alone gets "Unknown command. Type /help." |
| `/block`, `/del`, `/update`, `/exit` sent by a non-creator | sender alone gets "/block is for the group's creator. Type /help." (the other creator-only commands already reply on their own) |
| `/` alone, `//`, `/ help`, `/1`, `:/`, `see /usr/bin`, `and/or`, a URL | ordinary chat, stored and relayed |

Note that a message *starting* with a path (`/usr/bin …`) counts as a command.
A slash followed by a non-ASCII character (`/帮助`) is chat.

Unchanged: file messages, agent status envelopes, and agents' `CGR1` reply
envelopes (an unwrapped envelope keeps the old handling).

**Rate limits.** Commands from anyone but the creator go through the anti-flood
rate limits (docs/ANTI-FLOOD.md, rate only, no duplicate check), so a command
spammer can't make the group talk back without limit. A held or muted member's
commands are dropped like their chat. The creator's commands are never limited,
so `/unmute` always works.

Journal: `command refused from=<userid> len=<n>: <reply>`.

Code: `linux/service/source/Tools/CommandText.h` (the classifier),
`ChatRobotCmd::Do` (returns `kUnknownCommand` / `kCreatorOnly` with the reply in
`errMsg`), `CarrierRobot::addMessgae` (the gate). Tests:

    g++ -std=c++14 -I linux/service/source/Tools -o test_command_text linux/tools/test_command_text.cpp
    ./test_command_text

## /list and /help replies (feedback#26)

`/list [page]` sends one page: 20 members per page, `index: display name`
(`(online)` when online), with a `Members 21–40 of 507 (12 online)` header and
`… and N more. Type /list 3.` when there are more. Names are cut to 24 bytes at
a UTF-8 boundary and control characters become spaces, so a page is at most
953 bytes, under Carrier's 1,024-byte single message. Userids are never listed.
Before this, `/list` sent everything in one message: 16,699 bytes for the 507
members of group 1 (2026-11-24).

`/help` is a list with one command per line (`- /list [page] (/l): …`), joined
by real `\n`. The newlines were already sent before; an app showing it as one
paragraph is dropping them when rendering. The list form displays as lines in
both plain text and Markdown. Both replies go to the sender (the last argument
`Do()` appends), so `/help x` no longer replies to "x".

Tests: `linux/tools/test_member_list.cpp`:

    g++ -std=c++14 -I linux/service/source/Tools -o test_member_list linux/tools/test_member_list.cpp
