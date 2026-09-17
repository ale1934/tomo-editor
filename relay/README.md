# Tomo relay

Talk to **your own Tomo agent** from inside the editor — no cloud AI
account needed. The relay is a tiny message server (standard library only)
that you run somewhere reachable. Your editor drops off requests, Tomo
picks them up on a schedule (about once a minute) and posts replies, and
the editor fetches them.

## Keybindings

| Keys   | Action                                                     |
|--------|------------------------------------------------------------|
| Ctrl+T | Send the current file + an instruction to Tomo as a request |
| F4     | Check for Tomo's reply; applies the edit if there is one   |

The round trip is asynchronous: after Ctrl+T, give Tomo a minute, then
press F4. Each F4 consumes one reply; press it again if there are more.

## Setup

**1. Start the relay** (any machine that is reachable from the internet —
your laptop behind a tunnel works, or a small cloud host):

```sh
export TOMO_RELAY_SECRET="$(openssl rand -hex 32)"   # pick a long secret
python3 relay/server.py --port 8472
```

The server listens on `0.0.0.0:8472` and appends every message to
`relay-data/messages.jsonl`, so restarts don't lose anything.

**2. Make it reachable.** If the machine has no public address, expose it
with a tunnel, e.g. `ngrok http 8472` or `cloudflared tunnel --url
http://localhost:8472`. Note the public URL it gives you.

**3. Point the editor at it:**

```sh
export TOMO_RELAY_URL="https://<your-public-url>"
export TOMO_RELAY_SECRET="<the same secret>"
```

Then launch the editor from the repo root as usual. Ctrl+T sends,
F4 collects.

**4. Tell Tomo the URL and secret** (paste them in chat). Tomo sets up a
scheduled check that polls the relay about once a minute and answers
your requests.

## Protocol

All endpoints speak JSON and require the `X-Tomo-Secret` header, except
`/health`.

| Endpoint          | Description                                              |
|-------------------|----------------------------------------------------------|
| `GET /health`     | `{"ok": true}` — no auth, for uptime checks              |
| `POST /push`      | Append a message → `{"ok": true, "id": 42}`              |
| `GET /poll?since=<id>` | Messages with id greater than `<id>`               |

Push body: `{"from": "editor"|"tomo", "kind": "request"|"response"|"note",
"body": {...}, "in_reply_to": <id>|null}`.

A request body from the editor looks like:

```json
{"instruction": "add a comment", "file": "main.cpp", "language": "cpp",
 "cursor": {"line": 3, "col": 0}, "code": "<full file text>"}
```

A response body from Tomo looks like:

```json
{"message": "done", "new_code": "<full file text>"}
```

`new_code` may be `null` when Tomo only wants to say something.

## Files

| File                  | Purpose                                              |
|-----------------------|------------------------------------------------------|
| `relay/server.py`     | The relay itself (stdlib only)                       |
| `relay/relay_client.py` | What the C++ editor shells out to (send / poll)    |
| `relay/agent_relay.py`  | What Tomo's machine runs (poll / respond)          |
| `../relay_bridge.h`     | C++ bridge: `RelayAsk()` / `RelayCheck()`          |

## Notes

- Keep `TOMO_RELAY_SECRET` private — anyone with it can push messages
  that your editor will treat as Tomo's. Rotate it by restarting the
  server with a new value (and telling Tomo).
- The relay keeps the last 2000 messages in memory; everything is also
  in `messages.jsonl`.
- `relay/.tomo_*` files are local editor state (poll cursor, outstanding
  request ids) and are git-ignored.
