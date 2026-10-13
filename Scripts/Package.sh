#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - package (Linux host, for the Linux server target)
# ---------------------------------------------------------------------------
#  Cross-compiling a Linux dedicated server from a Windows host is supported by
#  the engine, but the release pipeline builds it on Linux so the binary it
#  ships is the binary it tested.
#
#      ./Package.sh Server            Linux dedicated server
#      ./Package.sh Server Development
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${GAME_DIR}/OneBraham.uproject"

UAT="${UE_UAT:-}"
if [ -z "${UAT}" ]; then
    for candidate in \
        "${HOME}/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh" \
        "/opt/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh"; do
        if [ -f "${candidate}" ]; then
            UAT="${candidate}"
            break
        fi
    done
fi

if [ -z "${UAT}" ] || [ ! -f "${UAT}" ]; then
    echo "[OB] Set UE_UAT to your RunUAT.sh path, e.g."
    echo "     export UE_UAT=~/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh"
    exit 1
fi

CONFIG="${2:-Shipping}"
OUTDIR="${GAME_DIR}/Build/LinuxServer"

echo "[OB] packaging OneBrahamServer for Linux (${CONFIG})"

"${UAT}" BuildCookRun \
    -project="${PROJECT}" \
    -noP4 \
    -platform=Linux \
    -serverconfig="${CONFIG}" \
    -server \
    -cook \
    -allmaps \
    -build \
    -stage \
    -pak \
    -iostore \
    -compressed \
    -archive \
    -archivedirectory="${OUTDIR}"

echo "[OB] packaged to ${OUTDIR}"
