# Measuring YouTube streaming chat (streamList)

YouTube offers two ways to read live chat:

| | `liveChatMessages.list` (polling) | `liveChatMessages.streamList` (streaming) |
|---|---|---|
| How | One request every few seconds | One long-lived connection; YouTube pushes messages |
| Quota | 5 units per request (documented) | **Not documented** |
| Latency | Refresh interval (default 8 s) | Near real time |
| Plugin | Default | Experimental: *Use streaming chat* in the dock |

The plugin uses the REST form of `streamList` (`GET https://youtube.googleapis.com/youtube/v3/liveChat/messages/stream`, from the API's discovery document). It needs no gRPC or HTTP/2, so it works with the libcurl that ships with OBS on every platform. It uses the same `youtube.readonly` login as polling.

Because Google does not publish the quota cost of a streaming connection or how long YouTube keeps one open, this mode logs every connection so both can be measured.

## Running the measurement

1. Open **Docks → Social Feed → Accounts → YouTube** and tick **Use streaming chat (experimental)**.
2. Note the **Queries per day** usage of your project in Google Cloud Console: *APIs & Services → YouTube Data API v3 → Quotas & System Limits*. The console updates with a delay of a few minutes.
3. Go live (an unlisted test stream is fine) and leave it running for at least an hour, ideally with some chat activity.
4. Stop streaming, wait ~10 minutes, and note the console usage again.
5. Open the log: **Advanced → Open streaming measurement log**, which opens `youtube-stream-log.csv` in the plugin's config folder.

Each CSV row is one connection:

| Column | Meaning |
|---|---|
| `started_utc`, `duration_s` | When it opened and how long it stayed open |
| `http_status`, `content_type` | Response status and framing (`application/json` array or `text/event-stream`) |
| `first_byte_ms` | Time until YouTube sent the first data |
| `bytes`, `responses`, `items` | Traffic, number of pushed responses, chat items |
| `end_reason` | `eof` (YouTube closed it), `idle` (5 minutes without data), `chat-ended`, `error`, `aborted` |
| `reason`, `error` | Google's error reason and message, if any |
| `assumed_cost`, `units_used_today_estimate` | The plugin's own budget accounting (see below) |

## Working out the real cost

While streaming is on, the plugin makes only these calls:

* `liveBroadcasts.list`: 1 unit, about once a minute while you are **not** live, and once when you go live.
* One `streamList` connection per CSV row.

So:

```
cost per connection ≈ (console usage after − before − broadcast checks) / number of CSV rows in that window
```

The OBS log (*Help → Log Files*) also has one `YouTube stream connection:` line per connection.

The plugin budgets streaming with **Advanced → Assumed cost per streaming connection** (default 5, the same guess other open-source clients use). Set it to the measured value afterwards so the daily budget and the "quota today" figure are right.

## What the results decide

* **Streaming clearly cheaper** (few connections per hour, low cost per connection): make streaming the default and keep polling as the fallback.
* **Similar or more expensive**: keep polling as the default; streaming still gives lower latency for users with spare quota.
* **Connections end every few seconds** (`duration_s` small, `end_reason` = `eof`): the plugin already backs off after repeated short streams, but streaming then offers no quota advantage.

If the endpoint is not available (three HTTP 400/404/405/501 answers in a row), the plugin switches to polling for the rest of the session and shows why in the dock.

Please share the CSV (it contains no chat content or tokens, only timings, counts and error codes), the console numbers, and the stream length.
