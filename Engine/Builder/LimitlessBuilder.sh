#!/usr/bin/env bash
set -euo pipefail

NodeExecutable="${LIMITLESS_BUILDER_NODE:-}"
if [[ -z "$NodeExecutable" ]]; then
    NodeExecutable="$(command -v node || true)"
fi
if [[ -z "$NodeExecutable" || ! -x "$NodeExecutable" ]]; then
    echo "Error: Node.js 22.6 or newer was not found. Add it to PATH or set LIMITLESS_BUILDER_NODE." >&2
    exit 1
fi

NodeVersion="$("$NodeExecutable" -p 'process.versions.node')"
NodeMajor="${NodeVersion%%.*}"
NodeRemainder="${NodeVersion#*.}"
NodeMinor="${NodeRemainder%%.*}"
if (( NodeMajor < 22 || (NodeMajor == 22 && NodeMinor < 6) )); then
    echo "Error: Node.js 22.6 or newer is required." >&2
    exit 1
fi

ScriptDirectory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
"$NodeExecutable" --no-warnings --experimental-strip-types "$ScriptDirectory/Source/Cli/CommandLine.ts" "$@"
