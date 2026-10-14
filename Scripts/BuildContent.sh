#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - build every piece of content (macOS or Linux host)
# ---------------------------------------------------------------------------
#      ./BuildContent.sh                  everything
#      ./BuildContent.sh --only weapons   just the weapons
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${GAME_DIR}/OneBraham.uproject"

EDITOR="${UE_EDITOR:-}"
if [ -z "${EDITOR}" ]; then
    # UE_ROOT first, then the usual installs. On a Mac the editor binary inside
    # the app bundle runs commandlets the way UnrealEditor-Cmd does elsewhere.
    for candidate in \
        "${UE_ROOT:-/nonexistent}/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
        "${UE_ROOT:-/nonexistent}/Engine/Binaries/Linux/UnrealEditor-Cmd" \
        "/Users/Shared/Epic Games/UE_5.5/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
        "${HOME}/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd" \
        "/opt/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd"; do
        if [ -x "${candidate}" ]; then
            EDITOR="${candidate}"
            break
        fi
    done
fi

if [ -z "${EDITOR}" ]; then
    echo "[OB] Set UE_ROOT to the folder that contains Engine/, or UE_EDITOR to the editor binary."
    exit 1
fi

echo "[OB] building content"
"${EDITOR}" "${PROJECT}" \
    -run=pythonscript \
    -script="Tools/Python/onebraham/run_all.py" \
    -unattended -nopause -stdout -FullStdOutLogOutput \
    -ExtraArgs="$*"
