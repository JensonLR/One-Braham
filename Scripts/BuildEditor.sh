#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - compile the editor (macOS or Linux host)
# ---------------------------------------------------------------------------
#  The shipped client and server are built on Windows and Linux (README_BUILD).
#  This script is for working on the game on a Mac or a Linux desktop: it
#  generates the IDE project and compiles OneBrahamEditor for this machine.
#
#      ./BuildEditor.sh                   compile OneBrahamEditor, Development
#      ./BuildEditor.sh --generate        also (re)generate the Xcode / Makefile project
#      ./BuildEditor.sh --dry-run         print the commands, run nothing
#
#  The engine is found from UE_ROOT (the folder that contains Engine/), or the
#  Epic Games Launcher's default install for UE 5.5 on a Mac.
#  Bash 3.2 (the macOS default) is enough.
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${GAME_DIR}/OneBraham.uproject"

GENERATE=0
DRY_RUN=0
for arg in "$@"; do
    case "${arg}" in
        --generate) GENERATE=1 ;;
        --dry-run) DRY_RUN=1 ;;
        *) echo "[OB] unknown switch: ${arg}"; exit 2 ;;
    esac
done

case "$(uname -s)" in
    Darwin) HOST=Mac ;;
    Linux) HOST=Linux ;;
    *) echo "[OB] this script runs on macOS or Linux; on Windows use Scripts\\BuildEditor.bat"; exit 2 ;;
esac

ROOT="${UE_ROOT:-}"
if [ -z "${ROOT}" ]; then
    for candidate in \
        "/Users/Shared/Epic Games/UE_5.5" \
        "${HOME}/UnrealEngine" \
        "/opt/UnrealEngine"; do
        if [ -d "${candidate}/Engine" ]; then
            ROOT="${candidate}"
            break
        fi
    done
fi

if [ -z "${ROOT}" ] || [ ! -d "${ROOT}/Engine" ]; then
    echo "[OB] Unreal Engine was not found. Set UE_ROOT to the folder that contains Engine/, e.g."
    echo "     export UE_ROOT=\"/Users/Shared/Epic Games/UE_5.5\""
    exit 1
fi

BATCH="${ROOT}/Engine/Build/BatchFiles/${HOST}"

if [ "${HOST}" = "Mac" ] && ! xcode-select -p >/dev/null 2>&1; then
    echo "[OB] Xcode is not set up. UE 5.5 needs Xcode 15.2 or newer (15.4+ recommended):"
    echo "     install it from the App Store, open it once, then run"
    echo "     sudo xcode-select -s /Applications/Xcode.app"
    exit 1
fi

run() {
    echo "[OB] $*"
    if [ "${DRY_RUN}" -eq 0 ]; then
        "$@"
    fi
}

if [ "${GENERATE}" -eq 1 ]; then
    run "${BATCH}/GenerateProjectFiles.sh" "-project=${PROJECT}" -game
fi

run "${BATCH}/Build.sh" OneBrahamEditor "${HOST}" Development "-project=${PROJECT}" -waitmutex

echo "[OB] OneBrahamEditor built. Open OneBraham.uproject, then run Scripts/BuildContent.sh."
