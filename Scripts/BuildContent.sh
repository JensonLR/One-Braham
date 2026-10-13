#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - build every piece of content (Linux host)
# ---------------------------------------------------------------------------
#      ./BuildContent.sh                  everything
#      ./BuildContent.sh --only weapons   just the weapons
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${GAME_DIR}/OneBraham.uproject"

EDITOR="${UE_EDITOR:-}"
if [ -z "${EDITOR}" ]; then
    for candidate in \
        "${HOME}/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd" \
        "/opt/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd"; do
        if [ -x "${candidate}" ]; then
            EDITOR="${candidate}"
            break
        fi
    done
fi

if [ -z "${EDITOR}" ]; then
    echo "[OB] Set UE_EDITOR to your UnrealEditor-Cmd path."
    exit 1
fi

echo "[OB] building content"
"${EDITOR}" "${PROJECT}" \
    -run=pythonscript \
    -script="Tools/Python/onebraham/run_all.py" \
    -unattended -nopause -stdout -FullStdOutLogOutput \
    -ExtraArgs="$*"
