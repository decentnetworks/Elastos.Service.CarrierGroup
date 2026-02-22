# Command Menu Standards and Implementation Plan

## Purpose

Define the platform standards we should follow for command menus and describe how to implement equivalent behavior for Beagle, OpenClaw, and CarrierGroup integration.

---

## 1) External standards

### Telegram Bot API

Telegram supports native bot command menus that can vary by scope and language.

- `setMyCommands` accepts:
  - `commands` (max 100)
  - `scope` (defaults to `BotCommandScopeDefault`)
  - `language_code`
- Supported command scopes include:
  - `BotCommandScopeDefault`
  - `BotCommandScopeAllPrivateChats`
  - `BotCommandScopeAllGroupChats`
  - `BotCommandScopeAllChatAdministrators`
  - `BotCommandScopeChat`
  - `BotCommandScopeChatAdministrators`
  - `BotCommandScopeChatMember`
- Telegram defines a strict precedence algorithm ("first matching command set wins") when deciding what a user sees in DM/group/admin contexts.
- Command naming/format constraints:
  - command text: `1-32`, lowercase letters/digits/underscore
  - description: `1-256`

Consequence:
- Telegram menu can be native, configurable, and different by chat/user/admin/language.

### Discord Application Commands

Discord supports native application commands (slash/context menu) with scope/context/permission controls.

- Commands are registered via HTTP.
- Scope:
  - global commands
  - guild commands
- Context controls:
  - `integration_types` (installation context)
  - `contexts` (interaction surfaces: `GUILD`, `BOT_DM`, `PRIVATE_CHANNEL`)
- Permissions:
  - `default_member_permissions`
  - per-command guild permission overwrites (user/role/channel)
  - users without permission do not see the command in picker

Consequence:
- Discord menu is native and can vary by guild, DM availability, and permissions.

---

## 2) What this means for our system

### Important distinction

There are two layers:

1. Native command menu visibility (Telegram/Discord client UI)  
2. Runtime command execution and `/help` output (bot logic)

These must be consistent, but they are not the same mechanism.

### Current behavior (observed)

- OpenClaw registers native commands for Telegram and Discord.
- Beagle path currently relies on text command handling (`/help`, `/status`, etc.) and does not have native platform menu registration (because Beagle is custom transport, not Telegram/Discord client).

---

## 3) Standards we should adopt

For cross-channel consistency, define one internal policy:

- Command visibility context dimensions:
  - channel: `telegram | discord | beagle`
  - chat type: `dm | group`
  - role: `creator | admin | member`
  - sender authorization: `allowed | denied`
- Command capability flags:
  - `visible_in_menu`
  - `executable`
  - `requires_owner`
  - `requires_group_context`
  - `dangerous`

Rules:

- Never expose dangerous group management commands in DM menu.
- Never show commands user cannot execute in that context.
- Keep native command registration aligned with runtime execution rules.

---

## 4) Implementation plan

### Phase A: unified command catalog

Create one source of truth for command metadata:

- command id/name/description
- supported channels
- scope and permission requirements
- menu visibility predicates

### Phase B: Telegram alignment

- Register multiple command sets via `setMyCommands` with scoped menus:
  - private chats
  - group chats
  - administrators
  - optional per-chat/per-member overrides when needed
- Ensure `/help` uses the same visibility predicates.

### Phase C: Discord alignment

- Register global/guild commands with proper `contexts` and `default_member_permissions`.
- Use guild permission overwrites where needed for role/channel restrictions.
- Ensure `/help` output matches Discord-visible command set per user context.

### Phase D: Beagle menu model (non-native channel)

Because Beagle has no native slash UI contract, implement a server-driven menu endpoint:

- Example: `GET /commands/menu?group_id=<id>&sender=<userid>&chat_type=<dm|group>`
- Returns only currently visible/executable commands for that sender/context.
- App uses this endpoint to render menu.
- `/help` should call the same backend selector to avoid mismatch.

### Phase E: safety and compatibility

- Keep backward compatibility for text commands.
- Keep command execution as final authority (never trust UI only).
- Add tests for visibility/execution matrix:
  - DM creator/admin/member
  - group creator/admin/member
  - unauthorized sender

---

## 5) Recommended rollout order

1. Finalize command catalog and visibility policy.
2. Implement shared selector used by `/help`.
3. Add Beagle menu endpoint and switch app menu rendering to it.
4. Improve Telegram scoped native command registration.
5. Improve Discord context/permission registration.
6. Add integration tests and log assertions.

---

## References

- Telegram Bot API:
  - https://core.telegram.org/bots/api#setmycommands
  - https://core.telegram.org/bots/api#botcommandscope
  - https://core.telegram.org/bots/api#determining-list-of-commands
- Discord Application Commands:
  - https://discord.com/developers/docs/interactions/application-commands#registering-a-command
  - https://discord.com/developers/docs/interactions/application-commands#permissions
  - https://discord.com/developers/docs/interactions/application-commands#contexts
