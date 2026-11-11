# A group's own beagles.eth record

Since 2026-11-11 a group registers and edits its own `<name>.beagles.eth`
record: **owner = the group's Carrier userid**, signed by the group's own key.
The ens-gateway (webchat-eth `docs/GROUP-RECORDS.md`) accepts it as an ordinary
personal record: the Carrier identity signs (XEdDSA) and `carrierUserId == owner`.

## Commands (sent to the group's chat)

| command | who | effect |
|---|---|---|
| `/ens register <name>` | creator | create or re-sign `<name>.beagles.eth`: owner + `texts.carrierUserId` = group userid, `texts.carrierAddress` = group address, `texts.displayName` = group nickname. Refused if the name belongs to someone else. The name is remembered in `service_text["ens.name"]`. |
| `/profile avatar <https-url>` | creator | set `texts.avatar` (one `https://` URL, max 512 chars) and re-sign |
| `/profile intro <text>` | creator | set `texts.description` (rest of the line, 1-200 chars) and re-sign |
| `/profile show`, `/ens show` | anyone | print the live record from the gateway |
| `/update <name>` | creator | rename the group; the name is the rest of the line (was: first word only) |
| `/signer add <userid>`, `/signer del <userid>` / `/signer list` | creator / anyone | optional signer list, signed by the group key (`GET :5000/group/signers`) |

Updates keep every other field the record holds (wallet binds, socials) and drop
`signers`/`signersProof` from the texts, so the record stays a personal record.
Text arguments are re-joined with single spaces (the parser splits on whitespace).

## What is signed

Exactly what beagle-app's `/api/ens-register` sends for a node's own identity:
`signature.message` = the record as JSON, signed by `carrier_identity_sign`
(libcarrier, in-process; the key never leaves the SDK), 128 lowercase hex chars;
POST `<gateway>/set` with `{...record, expiration, signature:{hash, signerAddress, message}}`.
Gateway: `https://ens-gateway.beaglechat.workers.dev`, override with env
`BEAGLE_ENS_GATEWAY`. HTTP goes through `/usr/bin/curl` (exec'd, no shell) on a
detached thread, so a slow gateway never blocks the Carrier callback.

The group key signs only bytes this service builds itself: the signer-list
payload and the group's own record. No path signs caller-supplied bytes.

The gateway has no delete: a test name stays until it is overwritten.

## Known gap

The manager (`linux/manager`, `libcarrierManager.so`) still builds its
`group_table` UPDATE by string concatenation. A group name with an apostrophe is
saved by the group service, but the manager's copy fails (`near "s": syntax error`).
