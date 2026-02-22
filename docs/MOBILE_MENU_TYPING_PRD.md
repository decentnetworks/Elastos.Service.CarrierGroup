# Mobile PRD: Slash Menu and Agent Typing Status

## 1) Objective

Implement Telegram/Discord-like UX in Beagle iOS/Android clients for:

- slash command menu (`/`)
- agent typing/thinking status
- correct DM vs group attribution using protocol metadata

## 2) Background

- Group messages for agents are wrapped with `CGP1`.
- Agent group replies are wrapped with `CGR1`.
- Agent transient status is wrapped with `CGS1` (group) and `BGS1` (DM).
- Original sender identity is in `origin.userid` (and optional nickname/user_info/friend_info).

## 3) User Stories

- As a user in DM, typing `/` shows agent commands only (no group-admin commands).
- As a group member, typing `/` shows group-safe commands based on my role.
- As a user, I can see when agent is thinking/typing/sending.
- As a user, I can identify the original sender in group messages by `userid`, plus name/avatar if known.

## 4) Functional Requirements

### 4.1 Conversation classification

- `CGP1` inbound => treat as `group`.
- `CGR1` inbound => treat as `group` reply.
- `CGS1` inbound => group status event.
- `BGS1` inbound => DM status event.
- Otherwise => regular DM message.

### 4.2 Sender attribution

- Group sender userid source of truth: `origin.userid`.
- Display name priority:
  1. `origin.friend_info.label`
  2. `origin.user_info.name`
  3. `origin.nickname`
  4. local contact cache
  5. `CarrierUser-<userid-prefix>`
- Avatar hint:
  - use `origin.user_info.has_avatar` + local cache key by `origin.userid`.

### 4.3 Slash menu UX

- Trigger: input starts with `/` or user taps slash icon.
- Data source:
  - Phase 1: local static baseline (`/help`, `/status`).
  - Phase 2: server-driven list from menu endpoint (recommended).
- Command selection inserts command text into input box and keeps focus.
- DM must never show dangerous group commands (`/exit`, `/agent add`, `/agent del`).

### 4.4 Typing/thinking UX

- Render status from `CGS1/BGS1` as ephemeral indicator above input area.
- Map states:
  - `typing` -> "Agent is typing..."
  - `thinking` -> "Agent is thinking..."
  - `tool` -> "Agent is using tools..."
  - `sending` -> "Agent is sending..."
  - `idle` -> clear indicator
  - `error` -> "Agent hit an error"
- Auto-expire on `status.ttl_ms`.
- Any real message in same conversation clears active status immediately.
- De-duplicate by `status.seq` per conversation.

## 5) Non-Functional Requirements

- No status bubble persisted in message history.
- Status rendering latency target: under 500ms from receive callback.
- No extra DB dependency between app and group service.
- Backward compatible with clients that do not parse status envelopes.

## 6) Data Model (Client-side)

- `ConversationState`
  - `chatType`: `dm|group`
  - `groupUserId?`
  - `groupAddress?`
- `ParticipantState`
  - `userid`
  - `displayName`
  - `avatarKey?`
- `AgentStatusState`
  - `conversationKey`
  - `state`
  - `phase?`
  - `seq?`
  - `expiresAtMs`

## 7) Acceptance Criteria

- DM `/` menu never includes group-only commands.
- Group `/` menu includes role-appropriate commands.
- Group messages display original sender userid correctly.
- Typing indicator appears on `CGS1/BGS1` and auto-clears by TTL.
- Status indicator clears on final reply message.
- Unknown envelope prefixes do not crash UI.

## 8) Rollout Plan

1. Parse `CGP1/CGR1/CGS1/BGS1` in shared message decoder.
2. Implement status state machine + TTL clear timer.
3. Implement phase-1 slash menu (local policy).
4. Add phase-2 dynamic menu endpoint integration.
5. Add telemetry for status shown/cleared and command menu usage.
6. A/B test message-completion perception improvements.

## 9) Open Questions

- Final server contract for dynamic command-menu endpoint (`/commands/menu`) and auth.
- Whether to expose message delivery receipts to client in same status surface.
- Whether group member role metadata should come from CarrierGroup or app policy.
