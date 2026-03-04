# Mobile Group/DM Classification Guide (iOS + Android)

## Scope

This guide defines one shared rule set for iOS and Android clients to classify Carrier conversations as:

- group
- direct message (DM)

It replaces legacy string heuristics (for example, `startsWith("G")`).

## Canonical identity fields

- Group identity:
  - `group.userid`
  - `group.address`
- Original sender in group:
  - `origin.userid`

Do not use `origin.friend_info` or `origin.user_info` to carry group identity.

## Trusted sources for group IDs

Populate a local `knownGroupIds` set only from trusted data:

1. Successful parse of `CGP1` / `CGR1` / `CGS1` where `chat_type=group`
2. Group list/create/join API responses

Never populate from plain text content.

Store both values when available:

- `group.userid`
- `group.address`

## Classification algorithm

For each inbound payload:

1. If valid `CGP1`/`CGR1`/`CGS1` and `chat_type=group`: classify as group.
2. Else if valid `BGS1` and `chat_type=direct`: classify as DM.
3. Else if `chatId` is in `knownGroupIds`: classify as group.
4. Else: classify as DM.

## Why this works

- Envelope parsing is authoritative when present.
- Cache fallback handles plain-text legacy or compatibility messages.
- No dependence on unstable ID prefixes/lengths.

## iOS sketch (Swift)

```swift
final class GroupIdentityCache {
    static let shared = GroupIdentityCache()
    private let key = "carrier.knownGroupIds.v1"
    private(set) var ids = Set<String>()

    private init() {
        ids = Set(UserDefaults.standard.stringArray(forKey: key) ?? [])
    }

    func remember(groupUserId: String?, groupAddress: String?) {
        var changed = false
        if let v = groupUserId, !v.isEmpty { changed = ids.insert(v).inserted || changed }
        if let v = groupAddress, !v.isEmpty { changed = ids.insert(v).inserted || changed }
        if changed { UserDefaults.standard.set(Array(ids), forKey: key) }
    }

    func isKnownGroup(_ chatId: String) -> Bool { ids.contains(chatId) }
}
```

## Android sketch (Kotlin)

```kotlin
object GroupIdentityCache {
    private const val PREF = "carrier_group_ids"
    private const val KEY = "known_group_ids"

    fun load(context: Context): MutableSet<String> {
        val sp = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        return (sp.getStringSet(KEY, emptySet()) ?: emptySet()).toMutableSet()
    }

    fun remember(context: Context, groupUserId: String?, groupAddress: String?) {
        val set = load(context)
        var changed = false
        if (!groupUserId.isNullOrBlank()) changed = set.add(groupUserId) || changed
        if (!groupAddress.isNullOrBlank()) changed = set.add(groupAddress) || changed
        if (changed) {
            context.getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .edit()
                .putStringSet(KEY, set)
                .apply()
        }
    }

    fun isKnownGroup(context: Context, chatId: String): Boolean = load(context).contains(chatId)
}
```

## UI attribution rules

- Group conversation identity: by `group.userid` or `group.address`.
- Sender label/avatar in group: by `origin.userid`.

If fallback path classifies via `knownGroupIds` only, keep sender attribution from local contact cache until next valid group envelope refreshes metadata.

## Migration checklist

1. Remove prefix/length group heuristics.
2. Add envelope-first classifier.
3. Add persistent `knownGroupIds` cache.
4. Insert cache updates on trusted envelope/API paths.
5. Keep `origin.*` for sender display in group UI.
6. Add tests:
   - valid `CGP1` -> group
   - malformed `CGP1` + unknown chatId -> DM
   - plain text + known group chatId -> group
   - `BGS1` -> DM
