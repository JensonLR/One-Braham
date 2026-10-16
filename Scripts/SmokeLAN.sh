#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - two-process LAN smoke test on one Mac (macOS host)
# ---------------------------------------------------------------------------
#  Starts a listen host on the tower, waits for it to have its own pawn, then
#  starts a client that joins it over 127.0.0.1. Both run -OBSmoke with
#  -OBSmokeExpectPlayers=2 (Source/OneBraham/Dev/OneBrahamSmokeTest.h): each
#  waits until its game state holds both players, logs every player's name,
#  team, squad and pawn location plus the net connection (address, lag, ping),
#  takes screenshots - the first one facing the other player - and logs PASS
#  or FAIL. The host holds after its verdict until the client has left.
#
#      ./SmokeLAN.sh                          uncooked: the editor binary, -game
#      ./SmokeLAN.sh --staged                 the staged Development app
#      ./SmokeLAN.sh --shots 3 --timeout 300 --port 7778
#      ./SmokeLAN.sh --steam                  leave Steam on (default: -nosteam)
#      ./SmokeLAN.sh --bots 0                 ?Bots=N on the host's URL (default:
#                                             the match settings' bot fill)
#      ./SmokeLAN.sh -- -Mode=FireDrill       extra switches for both games
#      ./SmokeLAN.sh --dedicated              a dedicated server (-server, no
#                                             player, no smoke of its own) and
#                                             one client expecting 1 player
#      ./SmokeLAN.sh --late 60                start the client 60 s after the
#                                             host is ready (join in progress)
#      ./SmokeLAN.sh --lag 150 --loss 2       the client's packets: +150 ms each
#                                             way, 2 % dropped (-PktLag/-PktLoss)
#
#  Without a Steam client the engine falls back to the NULL online subsystem
#  and the IP net driver anyway (Config/DefaultEngine.ini, the Online / Steam
#  block); -nosteam just skips the attempt and its steam:// noise.
#
#  OB_UE_EDITOR overrides the editor binary, OB_SMOKE_APP the staged one.
#  The GPU lock /tmp/ob_smoke.lock is taken for the run (OB_SMOKE_LOCK=""
#  skips it, for a caller that already holds it).
#
#  Exit code: 0 only when both games logged PASS. Logs are kept under
#  Saved/Smoke/ (lan_<stamp>_host.log, lan_<stamp>_client.log).
# ===========================================================================
set -uo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${GAME_DIR}/OneBraham.uproject"
EDITOR_BIN="${OB_UE_EDITOR:-${HOME}/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
STAGED_BIN="${OB_SMOKE_APP:-${GAME_DIR}/Saved/StagedBuilds/Mac/OneBraham.app/Contents/MacOS/OneBraham}"
LOCK_DIR="${OB_SMOKE_LOCK-/tmp/ob_smoke.lock}"

MAP="L_Persistent_OneBraham"
SHOTS=2
TIMEOUT=300
PORT=7777
STAGED=0
NOSTEAM=1
BOTS=""
DEDICATED=0
LATE=0
LAG=0
LOSS=0
EXTRA=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --staged)  STAGED=1; shift ;;
        --steam)   NOSTEAM=0; shift ;;
        --map)     MAP="${2:?--map needs a map name}"; shift 2 ;;
        --shots)   SHOTS="${2:?--shots needs a number}"; shift 2 ;;
        --timeout) TIMEOUT="${2:?--timeout needs a number of seconds}"; shift 2 ;;
        --port)    PORT="${2:?--port needs a number}"; shift 2 ;;
        --bots)    BOTS="${2:?--bots needs a number}"; shift 2 ;;
        --dedicated) DEDICATED=1; shift ;;
        --late)    LATE="${2:?--late needs a number of seconds}"; shift 2 ;;
        --lag)     LAG="${2:?--lag needs milliseconds}"; shift 2 ;;
        --loss)    LOSS="${2:?--loss needs a percentage}"; shift 2 ;;
        --)        shift; EXTRA+=("$@"); break ;;
        *)         EXTRA+=("$1"); shift ;;
    esac
done

case "${SHOTS}${TIMEOUT}${PORT}${BOTS}${LATE}${LAG}${LOSS}" in
    *[!0-9]*)
        echo "  [OB] --shots, --timeout, --port, --bots, --late, --lag and --loss take whole numbers"
        exit 1
        ;;
esac

if [ "${DEDICATED}" -ne 0 ] && [ "${STAGED}" -ne 0 ]; then
    # The staged app is the game target; a dedicated server is its own
    # (OneBrahamServer) target and package.
    echo "  [OB] --dedicated runs the uncooked editor binary as the server; it does not combine with --staged"
    exit 1
fi

if [ "${STAGED}" -ne 0 ]; then
    BIN="${STAGED_BIN}"
    BASE=()
else
    BIN="${EDITOR_BIN}"
    BASE=("${PROJECT}")
fi

if [ ! -f "${BIN}" ] || [ ! -x "${BIN}" ]; then
    echo "  [OB] No game binary at:"
    echo "         ${BIN}"
    if [ "${STAGED}" -ne 0 ]; then
        echo "  Package a Development build first, or point OB_SMOKE_APP at it."
    else
        echo "  Point OB_UE_EDITOR at UnrealEditor.app/Contents/MacOS/UnrealEditor."
    fi
    exit 1
fi

# A dedicated server is not a player: the one client is the whole match.
EXPECT=2
if [ "${DEDICATED}" -ne 0 ]; then
    EXPECT=1
fi

COMMON=(-windowed -ResX=960 -ResY=540 -unattended -nosplash -log -stdout -FullStdOutLogOutput
    -OBSmoke -OBSmokeShots="${SHOTS}" -OBSmokeExpectPlayers="${EXPECT}")
if [ "${STAGED}" -eq 0 ]; then
    COMMON=(-game "${COMMON[@]}")
fi
STEAM_SWITCH=()
if [ "${NOSTEAM}" -ne 0 ]; then
    STEAM_SWITCH=(-nosteam)
    COMMON+=(-nosteam)
fi

# Network emulation on the client only, so the host's view stays the baseline.
CLIENT_NET=()
if [ "${LAG}" -gt 0 ]; then
    CLIENT_NET+=(-PktLag="${LAG}")
fi
if [ "${LOSS}" -gt 0 ]; then
    CLIENT_NET+=(-PktLoss="${LOSS}")
fi

LOG_DIR="${GAME_DIR}/Saved/Smoke"
mkdir -p "${LOG_DIR}"
STAMP="$(date +%Y%m%d_%H%M%S)"
HOST_LOG="${LOG_DIR}/lan_${STAMP}_host.log"
CLIENT_LOG="${LOG_DIR}/lan_${STAMP}_client.log"

# Both games' budgets run from their own start; the minute is for engine
# start-up and a clean shutdown after the verdict.
DEADLINE=$((TIMEOUT + LATE + 90))
# The host's budget covers a late client's delay as well.
HOLD=$((TIMEOUT + 30))

HOST_PID=""
CLIENT_PID=""
HAVE_LOCK=0

stop_pid() {
    local pid="$1"
    [ -n "${pid}" ] || return 0
    kill -0 "${pid}" 2>/dev/null || return 0
    kill -TERM "${pid}" 2>/dev/null
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        kill -0 "${pid}" 2>/dev/null || return 0
        sleep 1
    done
    kill -KILL "${pid}" 2>/dev/null
}

cleanup() {
    stop_pid "${CLIENT_PID}"
    stop_pid "${HOST_PID}"
    if [ "${HAVE_LOCK}" -ne 0 ]; then
        rmdir "${LOCK_DIR}" 2>/dev/null
    fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM

if [ -n "${LOCK_DIR}" ]; then
    WAITED=0
    until mkdir "${LOCK_DIR}" 2>/dev/null; do
        if [ "${WAITED}" -eq 0 ]; then
            echo "  [OB] waiting for ${LOCK_DIR} (another smoke run holds the GPU)"
        fi
        if [ "${WAITED}" -ge 1800 ]; then
            echo "  [OB] gave up waiting for ${LOCK_DIR} after ${WAITED}s"
            exit 1
        fi
        sleep 5
        WAITED=$((WAITED + 5))
    done
    HAVE_LOCK=1
fi

echo
echo "  ONE BRAHAM - LAN smoke test (host + client on 127.0.0.1:${PORT})"
echo "  ================================================================"
echo "  binary   ${BIN}"
echo "  map      ${MAP}, ${SHOTS} shot(s) each, game budget ${TIMEOUT}s, bots ${BOTS:-match default}"
echo "  host     $([ "${DEDICATED}" -ne 0 ] && echo "dedicated server" || echo "listen server")," \
    "client joins $([ "${LATE}" -gt 0 ] && echo "${LATE}s late" || echo "at once")," \
    "client lag ${LAG} ms, loss ${LOSS} %"
echo "  logs     ${HOST_LOG}"
echo "           ${CLIENT_LOG}"
echo

HOST_URL="${MAP}?listen?Name=OBHost"
if [ -n "${BOTS}" ]; then
    HOST_URL="${HOST_URL}?Bots=${BOTS}"
fi

if [ "${DEDICATED}" -ne 0 ]; then
    # No -game, no window, no smoke subsystem (it is never created on a
    # dedicated server): the server is judged by the client and its own log.
    HOST_URL="${MAP}"
    if [ -n "${BOTS}" ]; then
        HOST_URL="${HOST_URL}?Bots=${BOTS}"
    fi
    # LogNet is at Warning in DefaultEngine.ini; raised here so the driver's
    # "listening on port" line - the readiness signal - reaches the log.
    "${BIN}" ${BASE+"${BASE[@]}"} "${HOST_URL}" -server -port="${PORT}" -unattended -log -stdout -FullStdOutLogOutput \
        -LogCmds="LogNet Log" \
        ${STEAM_SWITCH+"${STEAM_SWITCH[@]}"} ${EXTRA+"${EXTRA[@]}"} >"${HOST_LOG}" 2>&1 &
    READY_PATTERN="GameNetDriver .*listening on port"
else
    "${BIN}" ${BASE+"${BASE[@]}"} "${HOST_URL}" -port="${PORT}" "${COMMON[@]}" \
        -OBSmokeTimeout="$((TIMEOUT + LATE))" -OBSmokeHold="${HOLD}" -WinX=0 -WinY=40 \
        ${EXTRA+"${EXTRA[@]}"} >"${HOST_LOG}" 2>&1 &
    READY_PATTERN="\[OB\.SMOKE\] players: waiting"
fi
HOST_PID=$!

# The client goes in once the host has a pawn (a dedicated server: once its
# game driver listens): by then the map is loaded and the driver is up. A
# client that arrives first would only time out.
ELAPSED=0
until grep -qE "${READY_PATTERN}" "${HOST_LOG}" 2>/dev/null; do
    if ! kill -0 "${HOST_PID}" 2>/dev/null; then
        echo "  [OB] the host quit before it was ready:"
        grep -F "[OB.SMOKE]" "${HOST_LOG}" | sed -e 's/^.*\[OB\.SMOKE\]/    /'
        tail -n 5 "${HOST_LOG}" | sed -e 's/^/    | /'
        exit 1
    fi
    if [ "${ELAPSED}" -ge "${TIMEOUT}" ]; then
        echo "  [OB] the host was not ready after ${ELAPSED}s"
        exit 1
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done
echo "  host ready after ${ELAPSED}s - starting the client"

if [ "${LATE}" -gt 0 ]; then
    echo "  joining late: waiting ${LATE}s"
    sleep "${LATE}"
fi

# Keep an option on the address. A packaged Mac app given a bare
# "127.0.0.1:7777" as its first argument takes it for a URL to open: AppKit
# sends it an open-document Apple event before the engine is up, and the
# allocator's memory tracker crashes in it (SIGSEGV, no log at all). With
# "?Name=..." after it the argument is left alone.
"${BIN}" ${BASE+"${BASE[@]}"} "127.0.0.1:${PORT}?Name=OBClient" "${COMMON[@]}" \
    -OBSmokeTimeout="${TIMEOUT}" -WinX=980 -WinY=40 ${CLIENT_NET+"${CLIENT_NET[@]}"} \
    ${EXTRA+"${EXTRA[@]}"} >"${CLIENT_LOG}" 2>&1 &
CLIENT_PID=$!

# The host leaves on its own once the client has gone (its hold ends with the
# last connection). A client that died before connecting never becomes one,
# so the host is given a minute after the client exits and then stopped
# rather than left to sit out its whole budget.
ELAPSED=0
TIMED_OUT=0
CLIENT_GONE_AT=-1
while kill -0 "${HOST_PID}" 2>/dev/null || kill -0 "${CLIENT_PID}" 2>/dev/null; do
    if [ "${ELAPSED}" -ge "${DEADLINE}" ]; then
        TIMED_OUT=1
        echo "  [OB] no exit after ${DEADLINE}s - stopping both games"
        break
    fi
    if [ "${CLIENT_GONE_AT}" -lt 0 ] && ! kill -0 "${CLIENT_PID}" 2>/dev/null; then
        CLIENT_GONE_AT="${ELAPSED}"
        # A dedicated server never leaves on its own.
        if [ "${DEDICATED}" -ne 0 ]; then
            break
        fi
    fi
    if [ "${CLIENT_GONE_AT}" -ge 0 ] && [ $((ELAPSED - CLIENT_GONE_AT)) -ge 60 ]; then
        echo "  [OB] the client quit ${ELAPSED}s in and the host is still up a minute later - stopping it"
        break
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done
stop_pid "${CLIENT_PID}"
stop_pid "${HOST_PID}"

# verdict <role> <log>: prints the side's lines and screenshots, returns 0 on PASS.
verdict() {
    local role="$1" log="$2"
    echo "  ${role}:"
    if ! grep -F "[OB.SMOKE]" "${log}" | sed -e 's/^.*\[OB\.SMOKE\]/    /'; then
        echo "    (no [OB.SMOKE] lines - the game died before the game instance came up)"
    fi
    # An uncooked run loads the editor module from Binaries/: a build that is
    # relinking it at that moment leaves the game without one.
    if grep -qF "The game module 'OneBraham' could not be found" "${log}"; then
        echo "    (the OneBraham editor module was missing at start-up - was it being rebuilt? run again)"
    fi
    grep -F "[OB.SMOKE] shot " "${log}" | sed -e 's/^.*-> //' | while IFS= read -r shot; do
        if [ -f "${shot}" ]; then echo "    screenshot ${shot}"; else echo "    screenshot ${shot} (missing)"; fi
    done
    echo
    if grep -qF "[OB.SMOKE] FAIL" "${log}"; then
        return 1
    fi
    grep -qF "[OB.SMOKE] PASS" "${log}"
}

HOST_OK=0
CLIENT_OK=0
if [ "${DEDICATED}" -ne 0 ]; then
    # No smoke on the server: it passes when it ran its match, logged the
    # client in, and did not crash or assert.
    echo "  dedicated server:"
    grep -E "\[OB\] Online subsystem|\[OB\.MATCH\] (InitGame|PostLogin|Match ACTIVE)|GameNetDriver .*listening on port" "${HOST_LOG}" \
        | sed -E -e 's/^.*(LogOneBraham[A-Za-z]*|LogNet): /    /'
    if grep -qE "Fatal error|Assertion failed|=== Critical error ===" "${HOST_LOG}"; then
        echo "    (the server crashed or asserted - see ${HOST_LOG})"
    elif grep -qF "[OB.MATCH] PostLogin" "${HOST_LOG}"; then
        HOST_OK=1
    else
        echo "    (no player ever logged in)"
    fi
    echo
else
    verdict "host" "${HOST_LOG}" && HOST_OK=1
fi
verdict "client" "${CLIENT_LOG}" && CLIENT_OK=1

if [ "${TIMED_OUT}" -ne 0 ]; then
    echo "  [OB] LAN smoke test TIMED OUT"
    exit 1
fi

if [ "${HOST_OK}" -eq 1 ] && [ "${CLIENT_OK}" -eq 1 ]; then
    echo "  [OB] LAN smoke test passed: host and client share the match."
    exit 0
fi

echo "  [OB] LAN smoke test FAILED (host $([ "${HOST_OK}" -eq 1 ] && echo PASS || echo FAIL)," \
    "client $([ "${CLIENT_OK}" -eq 1 ] && echo PASS || echo FAIL))"
exit 1
