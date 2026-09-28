# Overlay protocol

Each Chat Feed / Event Display source owns a private obs-browser source that loads

```
http://127.0.0.1:<port>/overlay/chat.html?source=<token>     (Chat Feed)
http://127.0.0.1:<port>/overlay/events.html?source=<token>   (Event Display)
```

from the plugin's loopback server (`src/net/local-server.cpp`). The port is picked at startup and the token is random per source instance.

## Initial config

`GET /api/source/<token>/config` returns:

```json
{
  "kind": "chat",                       // or "events"
  "design": { ... },                    // resolved design, see design-schema.md
  "channels": [{ "platform": "twitch", "channel": "" }],
  "accounts": { "twitch": "mylogin" },  // logged-in account per platform
  "demo": false,                        // true only for the editor preview copy
  "version": "0.1.0"
}
```

For Event Display sources each event type (and variant) also gets `mediaUrl`/`soundUrl`: local files are registered with the server and served from `/media/<id>/<name>`. Only registered files are served, and Range requests are supported for video.

## Live updates

The plugin calls obs-browser's `javascript_event` proc handler on the private browser, which fires a `socialFeed` CustomEvent on `window`. `event.detail` is an envelope:

```json
{ "type": "config" | "chat" | "event" | "moderation", "payload": { ... } }
```

### chat

```json
{
  "id": "msg-id",
  "platform": "twitch",
  "channel": "somechannel",
  "channelId": "12345",
  "user": {
    "id": "111", "login": "user", "displayName": "User", "color": "#1E90FF",
    "roles": ["broadcaster" | "moderator" | "vip" | "subscriber"],
    "badges": [{ "id": "moderator", "version": "1", "url": "https://…", "title": "Moderator" }],
    "avatar": "https://…"                       // optional
  },
  "text": "raw text",
  "fragments": [{ "type": "text", "text": "hi " }, { "type": "emote", "text": "Kappa", "url": "https://…", "id": "25" }],
  "isAction": false,
  "firstMessage": false,
  "highlighted": false,
  "bits": 100,                                  // optional (Twitch)
  "paid": "$5.00",                              // optional (YouTube Super Chat / Sticker)
  "reply": { "id": "…", "user": "Name", "text": "…" },   // optional
  "timestamp": 1700000000000,
  "test": true                                  // optional: sample item
}
```

### event

```json
{
  "id": "…",
  "type": "follow | subscription | gift_sub | cheer | tip | raid | redemption | hype_train | membership | super_chat | gift",
  "platform": "twitch",
  "channel": "mychannel",
  "user": { "id": "…", "login": "…", "displayName": "…", "avatar": "https://…" },
  "amount": 500,              // bits, months, gift count, viewers, tip value, points, level …
  "formattedAmount": "500 bits",
  "count": 5,                 // gift count / raid viewers
  "months": 12,
  "tier": "1000 | 2000 | 3000 | prime",
  "reward": "Hydrate!",       // redemption title / gift name
  "recipient": "Name",        // single gifted sub
  "message": "…",
  "anonymous": false,
  "replay": true,             // optional: replayed from the dock
  "test": true,               // optional: sample item
  "timestamp": 1700000000000
}
```

### moderation

```json
{ "platform": "twitch", "channel": "c", "action": "delete_message | clear_user | clear_chat", "target": "message id | user id | \"\"" }
```

### config

A full config object (as above), sent whenever the source settings change, e.g. from the Design Editor.
