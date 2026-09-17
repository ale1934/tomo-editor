#!/usr/bin/env python3
"""Tomo AI bridge.

Called by the Tomo text editor (see ../ai_bridge.h) as:

    python3 ai/ai.py <request.json> <response.json>

Reads the request, calls an OpenAI-compatible chat completions API, and
writes the response. Only the Python standard library is used.

Request JSON:
    {
        "instruction": "what the user asked for",
        "file": "main.cpp",
        "language": "cpp",
        "cursor": {"line": 12, "col": 5},   # 0-based
        "code": "<full file content>"
    }

Response JSON:
    {
        "ok": true,
        "message": "<short summary for the user>",
        "new_code": "<complete updated file>" | null
    }
    or {"ok": false, "error": "<what went wrong>"}

Configuration via environment variables:
    TOMO_AI_API_KEY   API key (not needed for a local Ollama server)
    TOMO_AI_BASE_URL  e.g. https://api.openai.com/v1
                      (local Ollama: http://localhost:11434/v1)
    TOMO_AI_MODEL     e.g. gpt-4o-mini  (Ollama: qwen2.5-coder:7b)
    TOMO_AI_TIMEOUT   seconds to wait for the API (default 120)
"""

import json
import os
import sys
import urllib.request
import urllib.error

SYSTEM_PROMPT = """\
You are Tomo, an AI pair programmer built into the Tomo text editor.
The user is editing a file and gives you an instruction. You receive the
file name, programming language, cursor position (0-based line and column),
and the full current content of the file.

You MUST reply with exactly one JSON object and nothing else -- no markdown
fences, no commentary outside the JSON. The object has this shape:

{"message": "<one or two sentence summary of what you did, or your answer>",
 "new_code": "<the COMPLETE updated file content>" or null}

Rules:
- If the instruction asks for a code change, put the ENTIRE updated file in
  "new_code" (not a diff, not a snippet). Preserve everything unrelated.
- If the instruction is a question or needs no code change, set "new_code"
  to null and answer in "message".
- Keep "message" concise: it is shown in the editor's status bar.
"""

HISTORY_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            ".tomo_history.json")
MAX_HISTORY_MESSAGES = 20  # user+assistant pairs count as 2


def fail(response_path, error):
    with open(response_path, "w", encoding="utf-8") as f:
        json.dump({"ok": False, "error": error}, f)
    print(f"tomo-ai error: {error}", file=sys.stderr)
    sys.exit(1)


def load_history():
    try:
        with open(HISTORY_PATH, encoding="utf-8") as f:
            data = json.load(f)
            if isinstance(data, list):
                return data[-MAX_HISTORY_MESSAGES:]
    except (OSError, ValueError):
        pass
    return []


def save_history(history):
    try:
        with open(HISTORY_PATH, "w", encoding="utf-8") as f:
            json.dump(history[-MAX_HISTORY_MESSAGES:], f)
    except OSError:
        pass  # history is a nice-to-have, not fatal


def strip_fences(text):
    text = text.strip()
    if text.startswith("```"):
        # drop first fence line and optional trailing fence
        lines = text.split("\n")
        lines = lines[1:]
        if lines and lines[-1].strip().startswith("```"):
            lines = lines[:-1]
        text = "\n".join(lines).strip()
    return text


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <request.json> <response.json>",
              file=sys.stderr)
        sys.exit(2)
    req_path, resp_path = sys.argv[1], sys.argv[2]

    try:
        with open(req_path, encoding="utf-8") as f:
            req = json.load(f)
    except (OSError, ValueError) as e:
        fail(resp_path, f"could not read request: {e}")

    api_key = os.environ.get("TOMO_AI_API_KEY", "")
    base_url = os.environ.get("TOMO_AI_BASE_URL",
                              "https://api.openai.com/v1").rstrip("/")
    model = os.environ.get("TOMO_AI_MODEL", "gpt-4o-mini")
    try:
        timeout = float(os.environ.get("TOMO_AI_TIMEOUT", "120"))
    except ValueError:
        timeout = 120.0

    if not api_key and "localhost" not in base_url and "127.0.0.1" not in base_url:
        fail(resp_path,
             "TOMO_AI_API_KEY is not set (see ai/README.md for setup)")

    instruction = req.get("instruction", "")
    filename = req.get("file", "untitled.txt")
    language = req.get("language", "text")
    cursor = req.get("cursor", {"line": 0, "col": 0})
    code = req.get("code", "")

    user_content = (
        f"File: {filename}\n"
        f"Language: {language}\n"
        f"Cursor: line {cursor.get('line', 0) + 1}, "
        f"column {cursor.get('col', 0) + 1} (1-based)\n"
        f"Instruction: {instruction}\n"
        f"--- file content ---\n{code}"
    )

    history = load_history()
    messages = ([{"role": "system", "content": SYSTEM_PROMPT}]
                + history
                + [{"role": "user", "content": user_content}])

    payload = json.dumps({
        "model": model,
        "messages": messages,
        "temperature": 0.2,
    }).encode("utf-8")

    http_req = urllib.request.Request(
        base_url + "/chat/completions",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    if api_key:
        http_req.add_header("Authorization", f"Bearer {api_key}")

    try:
        with urllib.request.urlopen(http_req, timeout=timeout) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace")[:500]
        fail(resp_path, f"API HTTP {e.code}: {detail}")
    except urllib.error.URLError as e:
        fail(resp_path, f"could not reach API at {base_url}: {e.reason}")
    except (ValueError, TimeoutError) as e:
        fail(resp_path, f"bad API response: {e}")
    except Exception as e:  # e.g. socket.timeout
        fail(resp_path, f"API request failed: {e}")

    try:
        raw = body["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        fail(resp_path, "API returned an unexpected shape")

    try:
        parsed = json.loads(strip_fences(raw))
        message = str(parsed.get("message", "")).strip()
        new_code = parsed.get("new_code")
        if new_code is not None:
            new_code = str(new_code)
    except ValueError:
        # Model didn't return JSON; treat the whole reply as a message.
        message, new_code = raw.strip(), None

    if not message:
        message = "Done."

    # remember this exchange for follow-ups
    history.append({"role": "user", "content": instruction})
    history.append({"role": "assistant", "content": message})
    save_history(history)

    with open(resp_path, "w", encoding="utf-8") as f:
        json.dump({"ok": True, "message": message, "new_code": new_code}, f)


if __name__ == "__main__":
    main()
