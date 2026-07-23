#!/usr/bin/env bash
set -euo pipefail

RootDirectory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
Builder="$RootDirectory/Engine/Builder/LimitlessBuilder.sh"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "Error: Xcode project generation is only supported on macOS." >&2
    exit 1
fi

if [[ ! -x "$Builder" ]]; then
    echo "Error: LimitlessBuilder was not found or is not executable: $Builder" >&2
    exit 1
fi

cd -- "$RootDirectory"
"$Builder" xcode --platform Mac --config Debug --type Game "$@"
