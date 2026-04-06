#!/usr/bin/env bash
set -e # Exit early if any commands fail

# Load environment variables from .env
if [ -f "$(dirname "$0")/.env" ]; then
    export $(grep -v '^#' "$(dirname "$0")/.env" | xargs)
fi

exec $(dirname "$0")/build/Debug/friday.exe "$@"