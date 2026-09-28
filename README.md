# Social Feed for OBS Studio

Social Feed adds two sources to OBS Studio:

* **Chat Feed**: a styled, animated, multi-channel chat box.
* **Event Display**: an alert box that shows one event at a time (follows, subs, gifted subs, bits, tips, raids, redemptions, …) from a priority queue.

Both sources are designed in a pop-out **Design Editor** with a live preview, presets and a custom CSS tab. Accounts, event history and test buttons live in the **Social Feed** dock.

> Status: early development (0.1.0). Twitch and YouTube are wired up. Kick, TikTok, StreamElements, Streamlabs and Streamer.bot are registered in the UI and data model but not connected yet (they show "Coming soon").

## Features

### Chat Feed
* Width/height set on the source; everything else in the Design Editor.
* Channels: your own accounts and/or any other Twitch channel (reading Twitch chat needs no login). YouTube reads your own channel's active live chat.
* Layout: newest at top or bottom, alignment, inline or stacked name/message, badges, platform icon, avatars, timestamps.
* Text: font, size, weight, color, name color (user / platform / custom), shadow, outline, emote size.
* Bubbles: background, radius, padding, border, platform/user accent stripe, max width.
* Animations: 10 enter and 10 exit effects with duration and easing, plus smooth reflow of the other messages.
* Message lifetime: keep the last N messages, expire after X seconds, and/or remove messages that overflow the source.
* Filters: per platform, minimum role, bots, blocked users, commands, blocked words (hide or censor), links (show, redact or hide), emote-only, minimum length.
* Highlights: first-time chatters, mentions/keywords, broadcaster, moderators, VIPs, subscribers, cheers; pronouns (pronouns.alejo.io).
* Emotes: native Twitch emotes plus BetterTTV, FrankerFaceZ and 7TV (including zero-width stacking).
* Moderation sync: deleted messages, timeouts/bans and chat clears are removed from the overlay.

### Event Display
* One event at a time. Waiting events are ordered by priority (per event type), then arrival; the queue has a maximum size and can drop the lowest-priority event when full.
* Per event type: enable, priority, hold time, minimum amount, title/message templates (`{name}`, `{amount}`, `{months}`, `{tier}`, …), image/GIF/video, sound and volume, text-to-speech, and **variations by amount** (e.g. a different alert for 1000+ bits).
* Card, banner or minimal styles; media above, beside, behind or hidden; enter/exit animations; animated highlight text.
* Text-to-speech with voice, volume, minimum amount, maximum length and a blocked-words list.
* Audio (alert sounds, video audio, TTS) is played through the source, so it appears in the OBS mixer and can be monitored.

### Dock and testing
* **Accounts**: Twitch and YouTube login (device code flow), alert toggle, YouTube refresh interval and quota, connection status for each platform.
* **Events**: history of received events with **Replay**.
* **Test**: send a sample chat message or any event type to every source.
* The Design Editor preview can stream demo data and send test items without affecting the live source.

## Using it

1. Add a **Chat Feed** or **Event Display** source and set its width/height.
2. Click **Open Design Editor…** in the source properties. Changes show in the preview; press **Apply**/**OK** (or tick "Apply changes to the source live").
3. Open **Docks → Social Feed** to log in to Twitch and/or YouTube. Twitch chat from other channels works without logging in; alerts for your own channel need a login.

## Twitch application (Client ID)

Twitch login uses the OAuth **Device Code** flow, which needs a Client ID but no client secret or redirect server.

1. Register an application at <https://dev.twitch.tv/console/apps> (Client type: **Public**; the OAuth redirect URL is not used, `http://localhost` is fine).
2. Either build with `-DSOCIAL_FEED_TWITCH_CLIENT_ID=<your id>` or paste the ID into the dock (**Accounts → Twitch → Advanced**).

Requested scopes: `chat:read user:read:chat moderator:read:followers channel:read:subscriptions bits:read channel:read:redemptions channel:read:hype_train`.

Tokens are stored in the plugin's OBS config directory (`plugin_config/social-feed/accounts.json`) with owner-only permissions.

## YouTube (your own Google project)

YouTube chat, Super Chats, Super Stickers, new members, milestones, gifted memberships, deleted messages and bans come from the official YouTube Data API through **your own** Google Cloud project, so the API quota is yours:

1. Create a project at <https://console.cloud.google.com/>, and enable **YouTube Data API v3**.
2. Configure the OAuth consent screen (External; add yourself as a test user while the app is in testing).
3. Create an OAuth client of type **TVs and Limited Input devices**.
4. Paste its Client ID and Client secret into the dock (**Accounts → YouTube → Advanced**) and click **Log in…**. Scope requested: `youtube.readonly`.

The plugin finds your active broadcast (checking once a minute while you are offline), then reads its chat. Reading chat costs 5 quota units per request, so the **Chat refresh** interval decides how many hours of chat the daily quota covers (default 8 s ≈ 4.4 hours with the default 10,000 units/day). The dock shows the estimate and today's usage. The plugin stops before the budget runs out and resumes after midnight Pacific time. If Google grants your project more quota, raise **Daily quota** under Advanced. Messages sent before the plugin connected are not replayed.

**Experimental streaming chat.** Ticking **Use streaming chat (experimental)** replaces polling with a single long-lived `liveChatMessages.streamList` connection (REST form, no extra login). Messages arrive with lower latency and, if YouTube charges per connection rather than per message, with much lower quota use. Google does not document its quota cost yet, so every connection is logged for measurement. See [docs/youtube-streaming-experiment.md](docs/youtube-streaming-experiment.md). YouTube Jewels gifts appear as **Gift** events.

## How it works

```
 Twitch IRC (wss) ─┐                          ┌─> Chat Feed source ──> private obs-browser ──> chat.html
 Twitch EventSub ──┤
 YouTube API ──────┼─> providers ─> EventBus ─┤
 (future services)─┘                          └─> Event Display ─────> private obs-browser ──> events.html
                                                       ▲
               loopback HTTP server: overlay pages, per-source config, whitelisted media
```

* Each source wraps a private **obs-browser** source, so the look is plain HTML/CSS/JS (see `data/overlay/`). Items are pushed into the page with obs-browser's per-source `javascript_event` proc handler. See [docs/overlay-protocol.md](docs/overlay-protocol.md).
* The design is a JSON document stored in the source settings and deep-merged over the defaults; see [docs/design-schema.md](docs/design-schema.md).
* Networking uses **libcurl** (bundled with OBS on every platform, with TLS). WebSockets are implemented on top of curl's `CONNECT_ONLY` mode, because Qt's TLS backend and curl's own WebSocket API are not reliably available in OBS builds.
* Providers share one connection per platform no matter how many sources exist.

Source layout:

| Path | Purpose |
|------|---------|
| `src/core` | Feed item model, event bus + history, global config, sample data |
| `src/net` | curl HTTP client, WebSocket client, loopback overlay server |
| `src/providers` | Provider interface/manager, Twitch (auth, IRC chat, EventSub, Helix), YouTube (auth, quota, live chat), stubs for other platforms |
| `src/sources` | Chat Feed / Event Display source implementation and design defaults/themes |
| `src/ui` | Design Editor, live preview, Social Feed dock |
| `data/overlay` | Overlay pages, styles, animations, emotes, demo data |

## Building

This project uses the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate) build system. The template's documentation applies, including its [Quick Start Guide](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide).

```sh
cmake --preset ubuntu-x86_64            # or macos / windows-x64
cmake --build --preset ubuntu-x86_64
```

Requirements besides the template's: libcurl development files (Ubuntu: `libcurl4-openssl-dev`) and, on Linux, the Qt private headers (`qt6-base-private-dev`) for the Wayland preview. At runtime the sources need the **obs-browser** plugin, which ships with the official OBS builds.

## Roadmap

* YouTube: reading another channel's live chat (by video URL)
* Kick (chat, subs, gifted subs)
* TikTok (chat, gifts, follows)
* StreamElements, Streamlabs and Streamer.bot (tips and other events)
* Merging events into the Chat Feed as inline system messages

## License

GPL-2.0-or-later, see [LICENSE](LICENSE).
