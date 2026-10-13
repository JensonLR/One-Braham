#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - client (Linux, development)
# ---------------------------------------------------------------------------
#  Linux client development is supported for iteration and for the QA pass; the
#  shipped PC client is Windows. Steam is the distribution target, so a Linux
#  client build additionally needs Steam's Linux runtime.
#
#      ./RunClient.sh                    main menu
#      ./RunClient.sh 127.0.0.1          connect to a local server
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLIENT_BIN="${GAME_DIR}/Build/LinuxClient/OneBraham.sh"

if [ ! -f "${CLIENT_BIN}" ]; then
    echo "  OneBraham.sh was not found at:"
    echo "    ${CLIENT_BIN}"
    echo ""
    echo "  Package the Linux client first:"
    echo "    Scripts/Package.sh Client Linux"
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
