# Design schema

A source's design is stored as a JSON string in its settings (`design`) and deep-merged over the defaults in `src/sources/design-defaults.cpp`, which are the authoritative list of keys and default values. Presets are files of the form `{ "kind": "chat" | "events", "design": { ... } }`. They are saved under `plugin_config/social-feed/presets/{chat,events}/` and can be imported and exported from the editor.

Colors are `#RRGGBB` or `#RRGGBBAA`. Sizes are pixels unless noted. Animation names are the ones listed in `data/overlay/css/animations.css` (`sf-in-*`, `sf-out-*`), and easing is any CSS timing function.

## Chat Feed

| Key | Meaning |
|-----|---------|
| `layout.direction` | `newest-bottom` or `newest-top` |
| `layout.align` | `left`, `center`, `right` |
| `layout.messageLayout` | `inline` (name and text on one line) or `stacked` |
| `layout.gap`, `layout.padding` | spacing |
| `layout.showBadges`, `showPlatformIcon`, `showAvatars`, `showTimestamps` | toggles |
| `layout.nameSuffix` | text after the name (e.g. `:`) |
| `text.*` | font family/size/weight, line height, color, name weight, `nameColorMode` (`user`, `platform`, `custom`), `nameColor`, `emoteScale` (× font size), shadow, outline |
| `bubble.*` | `enabled`, background, radius, padding, border, `accent` (`none`, `platform`, `user`), `accentWidth`, `maxWidth` (%) |
| `animation.*` | `in`, `inDuration`, `inEasing`, `out`, `outDuration`, `outEasing`, `reflow` |
| `lifetime.maxMessages` | 0 = unlimited |
| `lifetime.expireSeconds` | 0 = never |
| `lifetime.removeOverflow` | remove messages that no longer fit |
| `filters.platforms.<id>` | per-platform toggle |
| `filters.minRole` | `everyone`, `subscriber`, `vip`, `moderator`, `broadcaster` |
| `filters.hideBots`, `bots[]`, `blockedUsers[]` | user filters |
| `filters.hideCommands`, `commandPrefixes` | e.g. `"!"` |
| `filters.blockedWords[]`, `blockedWordAction` | `hide` or `censor` |
| `filters.links` | `show`, `redact`, `hide-message` |
| `filters.hideEmoteOnly`, `minLength` | |
| `highlights.<firstTime|mention|broadcaster|moderator|vip|subscriber|cheer>` | `{ enabled, color }`; `mention.keywords[]` |
| `highlights.pronouns` | show Twitch pronouns |
| `emotes.bttv`, `ffz`, `sevenTv`, `animated` | third-party emotes |
| `customCss` | appended after the generated styles |

## Event Display

| Key | Meaning |
|-----|---------|
| `layout.style` | `card`, `banner`, `minimal` |
| `layout.horizontal`, `vertical` | position inside the source |
| `layout.textAlign` | `left`, `center`, `right` |
| `layout.mediaPosition` | `top`, `left`, `right`, `background`, `none` |
| `layout.mediaSize`, `padding`, `gap`, `maxWidth` (%) | |
| `text.*` | font, title size/weight/color, `highlightColor` (names/amounts), message size/color, shadow, `textAnimation` (`none`, `wave`, `pulse`, `bounce`, `rainbow`) |
| `card.*` | background, radius, border |
| `animation.*` | enter/exit effect, duration, easing |
| `queue.holdSeconds` | default time on screen |
| `queue.gapSeconds` | pause between events |
| `queue.maxQueue`, `dropLowestWhenFull` | queue limits |
| `tts.*` | `enabled`, `voice`, `volume`, `minAmount`, `maxLength`, `blockedWords[]`, `readName`, `delayMs` |
| `platforms.<id>` | per-platform toggle |
| `types.<type>` | `enabled`, `priority` (higher first), `holdSeconds` (0 = default), `minAmount`, `title`, `message`, `media`, `sound`, `volume`, `tts`, `variants[]` |
| `types.<type>.variants[]` | `{ minAmount, title, message, media, sound }`: the variant with the highest `minAmount` that the event reaches overrides the non-empty fields |
| `customCss` | appended after the generated styles |

Template placeholders: `{name} {amount} {formattedAmount} {count} {months} {tier} {reward} {recipient} {message} {platform}`.
