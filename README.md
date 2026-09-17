# tomo-editor

A minimal text editor written in C++ with raylib.

Build with `./build.sh`, run `./bin/tap [file]`.

## AI integration

Press **Ctrl+K**, type an instruction, and the AI reads your current file
and cursor position — then answers or rewrites the code for you.
See [ai/README.md](ai/README.md) for setup (API key or local Ollama).

## Tomo relay

Talk to **your own Tomo agent** instead of a cloud AI: **Ctrl+T** sends
the current file + an instruction as a request, **F4** collects Tomo's
reply (about a minute later). See [relay/README.md](relay/README.md)
for setup — you run a tiny relay server and share its URL + secret
with Tomo.
