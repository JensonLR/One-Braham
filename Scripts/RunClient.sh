#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - client (Linux, development)
# ---------------------------------------------------------------------------
#  For iteration and the QA pass against a Linux server; the shipped client is
#  Windows. Steam's Linux runtime is needed for the online subsystem.
#
#      ./RunClient.sh                    main menu
#      ./RunClient.sh 127.0.0.1          connect to a local server
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLIENT_BIN="${GAME_DIR}/Build/LinuxClient/Linux/OneBraham.sh"

if [ ! -f "${CLIENT_BIN}" ]; then
    echo "  OneBraham.sh was not found at:"
    echo "    ${CLIENT_BIN}"
    echo ""
    echo "  Package the Linux development client first:"
    echo "    Scripts/Package.sh Client Development"
    exit 1
fi

if [ "$#" -eq 0 ]; then
    echo "[OB] launching the client to the main menu"
    exec "${CLIENT_BIN}"
fi

ADDRESS="$1"
shift || true
echo "[OB] connecting to ${ADDRESS}"
exec "${CLIENT_BIN}" "${ADDRESS}" -log "$@"
