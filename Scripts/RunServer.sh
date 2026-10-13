#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - dedicated server (Linux)
# ---------------------------------------------------------------------------
#  The shipping target for a rented box. Linux server, no renderer, 60 Hz net
#  tick. Run it under systemd or screen; it logs to stdout and to
#  Saved/Logs/OneBrahamServer.log.
#
#      ./RunServer.sh                          ASCENT on the persistent map
#      ./RunServer.sh -Port=7810               a second instance on one box
#      ./RunServer.sh -Mode=FireDrill          FIRE DRILL, 6v6
#
#  A note on the firewall: a UE server needs 7777/udp for game traffic and
#  7778/udp for voice. Both are opened by the game's own net driver config, but
#  a hosting provider's security group has to allow them too.
# ===========================================================================
set -euo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_BIN="${GAME_DIR}/Build/LinuxServer/OneBrahamServer.sh"

if [ ! -f "${SERVER_BIN}" ]; then
    echo "  OneBrahamServer.sh was not found at:"
    echo "    ${SERVER_BIN}"
    echo ""
    echo "  Package the Linux server first:"
    echo "    Scripts/Package.sh Server"
    exit 1
fi

DEFAULT_ARGS="OneBrahamServer-L_Persistent_OneBraham -server -log -SteamServerName=One-Braham-London -MaxPlayers=16"

echo "[OB] starting dedicated server"
exec "${SERVER_BIN}" ${DEFAULT_ARGS} "$@"
