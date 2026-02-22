# Agent-Client Communication and Status Protocol

## Scope

This document defines the wire contract between:

- CarrierGroup service
- beagle-sidecar
- openclaw-beagle-channel
- mobile/desktop Beagle clients

The goal is to preserve sender attribution for group chat and support ephemeral agent status (`typing`, `thinking`, etc.) without local DB sharing between components.

---

## 1) Message envelopes

### 1.1 Group -> agent message (`CGP1`)

Prefix:

```text
CGP1 <json>
```

Required JSON fields:

- `type`: `carrier_group_message`
- `chat_type`: `group`
- `group.userid`, `group.address`
- `origin.userid` (original sender in group)
- `message.text`

Optional:

- `origin.nickname`
- `origin.user_info` / `origin.friend_info`
- `group.nickname`
- `message.timestamp`

### 1.2 Agent -> group reply (`CGR1`)

Prefix:

```text
CGR1 <json>
```

Required JSON fields:

- `type`: `carrier_group_reply`
- `chat_type`: `group`
- `message.text`

Optional:

- `group.userid`, `group.address`

### 1.3 Agent status envelope (`CGS1` for group, `BGS1` for DM)

Prefixes:

```text
CGS1 <json>   # group-related status
BGS1 <json>   # DM-related status
```

Required JSON fields:

- `type`: `agent_status`
- `chat_type`: `group` or `direct`
- `status.state`: one of `typing|thinking|tool|sending|idle|error`
- `status.ttl_ms`

Optional:

- `status.phase`
- `status.seq`
- `status.ts`
- `group.userid`, `group.address`, `group.name` (when `chat_type=group`)
- `origin.userid` (added by CarrierGroup relay)

Example:

```json
{
  "type": "agent_status",
  "version": 1,
  "chat_type": "group",
  "group": {
    "userid": "<group_userid>",
    "address": "<group_address>"
  },
  "origin": {
    "userid": "<agent_userid>"
  },
  "status": {
    "state": "typing",
    "phase": "thinking",
    "ttl_ms": 12000,
    "seq": "c6f6f7a8"
  },
  "origin": {
    "userid": "AGENT_USERID"
  }
}
```

---

## 2) Current implementation status (2026-02-22)

Implemented:

- `openclaw-beagle-channel`
  - emits status lifecycle callbacks (`typing`, `sending`, `idle`, `error`) during dispatch
  - posts status via sidecar `POST /sendStatus`
- `beagle-sidecar`
  - added `POST /sendStatus`
  - emits `CGS1` or `BGS1` prefixed status message
- `CarrierGroup`
  - accepts `CGS1` from registered agents
  - normalizes payload
  - fans out to non-agent group members
  - does not store status envelopes in message history

Pending:

- mobile app parser/UX for `CGS1`/`BGS1`
- command menu API for dynamic `/` list

---

## 3) Sidecar HTTP interface (implemented)

- `GET /events`
- `POST /sendText`
- `POST /sendMedia`
- `POST /sendStatus`

`POST /sendStatus` request body:

```json
{
  "peer": "TARGET_USERID",
  "state": "typing",
  "phase": "thinking",
  "ttlMs": 12000,
  "chatType": "group",
  "groupUserId": "GROUP_USERID",
  "groupAddress": "GROUP_ADDRESS",
  "groupName": "GROUP_NAME",
  "seq": "abc123"
}
```

Response:

```json
{"ok": true}
```

---

## 4) Client handling rules

- Treat `CGS1/BGS1` as transient UI state, not transcript message.
- Apply `status.ttl_ms`; auto-clear when expired.
- Clear active status immediately when a text/media reply from same conversation arrives.
- Use `origin.userid` (group) or peer userid (DM) for actor identity.
- Fallback: if a client does not support `CGS1/BGS1`, it may show raw text; deploy parser before broad status sending.
