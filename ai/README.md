# Tomo AI integration

Press **Ctrl+K** inside the editor, type an instruction (e.g.
`add error handling to the save function` or `what does this file do?`),
and the AI will read your current file plus cursor position and either
answer or rewrite the code for you. Edits replace the buffer — press
**Ctrl+S** to keep them.

## How it works

```
editor (C++) --Ctrl+K--> ai_bridge.h --system()--> ai/ai.py --HTTPS--> LLM API
                    <-- applies new_code --  <-- JSON response <--
```

- `ai_bridge.h` builds a JSON request `{instruction, file, language,
  cursor, code}` and shells out to `ai/ai.py`. No new C++ dependencies.
- `ai/ai.py` (standard library only) calls any **OpenAI-compatible**
  chat completions endpoint and writes back `{message, new_code}`.
- Recent exchanges are kept in `ai/.tomo_history.json` so follow-up
  instructions have context. Delete that file to start fresh.

## Setup

The editor must be launched from the repo root so it can find `ai/ai.py`.

Configure with environment variables:

| Variable           | Default                    | Example (local Ollama)          |
|--------------------|----------------------------|---------------------------------|
| `TOMO_AI_API_KEY`  | *(required for remote)*    | *(not needed for Ollama)*       |
| `TOMO_AI_BASE_URL` | `https://api.openai.com/v1`| `http://localhost:11434/v1`     |
| `TOMO_AI_MODEL`    | `gpt-4o-mini`              | `qwen2.5-coder:7b`              |
| `TOMO_AI_TIMEOUT`  | `120` (seconds)            |                                 |

OpenAI:

```sh
export TOMO_AI_API_KEY="sk-..."
./bin/tap
```

Free local option with [Ollama](https://ollama.com):

```sh
ollama pull qwen2.5-coder:7b
export TOMO_AI_BASE_URL="http://localhost:11434/v1"
export TOMO_AI_MODEL="qwen2.5-coder:7b"
./bin/tap
```

## Troubleshooting

- `AI helper failed: ...` in the status bar — the exact reason is shown;
  common causes are a missing `TOMO_AI_API_KEY` or no network route.
- `is python3 installed?` — the helper needs `python3` on your `PATH`.
- Full stderr from the helper is left in `/tmp/tomo_ai_stderr.log`.
