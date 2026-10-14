#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - dedicated server (Linux)
# ---------------------------------------------------------------------------
#  The shipping target for a rented box: no renderer, 60 Hz net tick. Run it
#  under systemd or screen; it logs to stdout and to the packaged build's
#  OneBraham/Saved/Logs/OneBraham.log.
#
#      ./RunServer.sh                          ASCENT on the persistent map
#      ./RunServer.sh -Port=7810               a second instance on one box
#      ./RunServer.sh -Mode=FireDrill          FIRE DRILL
#      MAP=L_FireDrill ./RunServer.sh          another map from the cook list
#
#  Firewall: 7777/udp for game traffic (or the -Port you pass). Voice travels
#  inside the game connection, so there is no second port to open.
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_BIN="${GAME_DIR}/Build/LinuxServer/LinuxServer/OneBrahamServer.sh"
MAP="${MAP:-L_Persistent_OneBraham}"

if [ ! -f "${SERVER_BIN}" ]; then
    echo "  OneBrahamServer.sh was not found at:"
    echo "    ${SERVER_BIN}"
    echo ""
    echo "  Package the Linux server first:"
    echo "    Scripts/Package.sh Server"
    exit 1
fi

echo "[OB] starting dedicated server on ${MAP}"
exec "${SERVER_BIN}" "${MAP}" -server -log -SteamServerName=One-Braham-London -MaxPlayers=16 "$@"
