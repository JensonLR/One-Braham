#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - package on a Linux host
# ---------------------------------------------------------------------------
#  The release pipeline builds the Linux dedicated server on Linux, so the
#  binary it ships is the binary it tested. A Linux client is a development
#  build for iteration only; the shipped client is Windows.
#
#      ./Package.sh Server                    Linux dedicated server, Shipping
#      ./Package.sh Server Development
#      ./Package.sh Client Development        Linux development client
#
#  Output:
#      Build/LinuxServer/LinuxServer/OneBrahamServer.sh
#      Build/LinuxClient/Linux/OneBraham.sh
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${GAME_DIR}/OneBraham.uproject"

UAT="${UE_UAT:-}"
if [ -z "${UAT}" ] && [ -n "${UE_ROOT:-}" ]; then
    UAT="${UE_ROOT}/Engine/Build/BatchFiles/RunUAT.sh"
fi
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
    echo "[OB] Set UE_ROOT to the folder that contains Engine/, or UE_UAT to RunUAT.sh, e.g."
    echo "     export UE_ROOT=~/UnrealEngine"
    exit 1
fi

TARGET="${1:-Server}"
CONFIG="${2:-Shipping}"

COMMON=(BuildCookRun "-project=${PROJECT}" -noP4 -utf8output -unattended
        -platform=Linux -build -cook -stage -pak -iostore -compressed -archive)

case "${TARGET}" in
    Server)
        OUTDIR="${GAME_DIR}/Build/LinuxServer"
        echo "[OB] packaging OneBrahamServer for Linux (${CONFIG})"
        # -server -noclient is what makes this a server package: without it
        # BuildCookRun packages a client, which this script used to do.
        "${UAT}" "${COMMON[@]}" -server -noclient -serverplatform=Linux \
            "-serverconfig=${CONFIG}" "-archivedirectory=${OUTDIR}"
        EXE="${OUTDIR}/LinuxServer/OneBrahamServer.sh"
        ;;
    Client)
        OUTDIR="${GAME_DIR}/Build/LinuxClient"
        echo "[OB] packaging the Linux development client (${CONFIG})"
        "${UAT}" "${COMMON[@]}" "-clientconfig=${CONFIG}" "-archivedirectory=${OUTDIR}"
        EXE="${OUTDIR}/Linux/OneBraham.sh"
        ;;
    *)
        echo "[OB] Unknown target '${TARGET}'. Use Server or Client."
        exit 1
        ;;
esac

if [ ! -f "${EXE}" ]; then
    echo "[OB] BuildCookRun finished but ${EXE} does not exist; check the archive step."
    exit 1
fi

echo "[OB] packaged: ${EXE}"
