#!/usr/bin/env bash
set -u

PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"
PATCH="$PATCH_DIR/core-gameobject-faction-hook.patch"

CORE_FILE_1="src/server/game/Entities/GameObject/GameObject.cpp"
CORE_FILE_2="src/server/game/Scripting/ScriptDefines/AllGameObjectScript.cpp"
CORE_FILE_3="src/server/game/Scripting/ScriptDefines/AllGameObjectScript.h"
CORE_FILE_4="src/server/game/Scripting/ScriptMgr.h"

for f in "$CORE_FILE_1" "$CORE_FILE_2" "$CORE_FILE_3" "$CORE_FILE_4"; do
  if ! [ -f "$f" ]; then
    echo "ERROR: $f not found. Run this from the AzerothCore repo root."
    exit 1
  fi
 done

# If the patch is already applied, reverse-check will succeed.
if git apply --reverse --check "$PATCH" >/dev/null 2>&1; then
  echo "OK: already applied: $(basename "$PATCH")"
  exit 0
fi

# If it is not applied yet, ensure it can be applied cleanly.
if git apply --check "$PATCH" >/dev/null 2>&1; then
  git apply "$PATCH"
  echo "APPLIED: $(basename "$PATCH")"
  exit 0
fi

echo "ERROR: cannot apply cleanly: $(basename "$PATCH")" >&2
echo "Hint: check local core modifications: (git status && git diff)" >&2
exit 1
