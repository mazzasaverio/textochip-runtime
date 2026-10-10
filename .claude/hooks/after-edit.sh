#!/usr/bin/env bash
# Runs `make check-fast` after every file edit, if the target exists.
# On failure, hands the last lines of the error to Claude so it can fix them right away.
set -u
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
cat >/dev/null  # consume the hook's JSON input

if [ ! -f Makefile ] || ! make -n check-fast >/dev/null 2>&1; then
  exit 0
fi

output=$(make -s check-fast 2>&1)
status=$?
if [ $status -ne 0 ]; then
  printf '%s' "$output" | tail -n 40 | python3 -c '
import json, sys
msg = "make check-fast failed after the edit. Fix it before continuing:\n" + sys.stdin.read()
print(json.dumps({"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": msg}}))
'
fi
exit 0
