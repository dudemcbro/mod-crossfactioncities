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

# Idempotency check: if the hook is already present, skip
if grep -R --line-number -E "OnGameObjectFactionCalc\\(" "$CORE_FILE_1" "$CORE_FILE_2" "$CORE_FILE_3" "$CORE_FILE_4" >/dev/null 2>&1; then
  echo "Hook already present. Skipping."
  exit 0
fi

echo "Applying patch: $PATCH"
git apply "$PATCH"
echo "Patch applied."
