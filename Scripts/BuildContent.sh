#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - build every piece of content (Linux / macOS host)
# ---------------------------------------------------------------------------
#      ./BuildContent.sh                  everything
#      ./BuildContent.sh --only weapons   just the weapons
#      ./BuildContent.sh --only match_settings
#                                         just the three DA_MatchSettings_<Mode>
#
#  UE_EDITOR overrides the editor search with an explicit UnrealEditor-Cmd.
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${GAME_DIR}/OneBraham.uproject"

EDITOR="${UE_EDITOR:-}"
if [ -z "${EDITOR}" ]; then
    if [ "$(uname -s)" = "Darwin" ]; then
        # Launcher installs land in /Users/Shared/Epic Games by default, or
        # wherever the user pointed it (often ~/UE_5.x). Newest version wins.
        CANDIDATES=()
        for root in "${HOME}" "/Users/Shared/Epic Games"; do
            for engine in "${root}"/UE_5.*; do
                [ -d "${engine}" ] && CANDIDATES=("${engine}/Engine/Binaries/Mac/UnrealEditor-Cmd" "${CANDIDATES[@]+"${CANDIDATES[@]}"}")
            done
        done
    else
        CANDIDATES=(
            "${HOME}/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd"
            "/opt/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd")
    fi

    for candidate in "${CANDIDATES[@]+"${CANDIDATES[@]}"}"; do
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

# The pythonscript commandlet runs from the engine's Binaries directory and
# reads -script= as one string, so a project path with spaces (or a relative
# one) never reaches Python intact. A one-file bootstrap in a space-free temp
# directory carries the real paths and switches in instead.
BOOTSTRAP_DIR="$(mktemp -d)"
trap 'rm -rf "${BOOTSTRAP_DIR}"' EXIT
BOOTSTRAP="${BOOTSTRAP_DIR}/ob_build_content.py"

python3 - "${GAME_DIR}" "${BOOTSTRAP}" "$@" <<'PY'
import json, sys
game_dir, bootstrap, switches = sys.argv[1], sys.argv[2], sys.argv[3:]
with open(bootstrap, "w") as handle:
    handle.write(
        "import sys\n"
        "sys.path.insert(0, {tools!r})\n"
        "from onebraham import run_all\n"
        "if run_all.build_all({switches}) != 0:\n"
        "    raise RuntimeError('ONE BRAHAM content build failed - see the report above')\n"
        .format(tools=game_dir + "/Tools/Python", switches=json.dumps(switches)))
PY

echo "[OB] building content with ${EDITOR}"
"${EDITOR}" "${PROJECT}" \
    -run=pythonscript \
    -script="${BOOTSTRAP}" \
    -unattended -nopause -stdout -FullStdOutLogOutput
