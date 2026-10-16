#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - unattended smoke test of the staged Mac build (macOS host)
# ---------------------------------------------------------------------------
#  Boots the packaged game with -OBSmoke, waits for it to quit, and reads the
#  verdict out of its log. The game side is UOneBrahamSmokeTestSubsystem
#  (Source/OneBraham/Dev/OneBrahamSmokeTest.h): it waits for the player's pawn,
#  logs one [OB.SMOKE] line per check, takes screenshots turning 90 degrees
#  between them, logs PASS or FAIL and exits. Development and Test builds only:
#  Shipping compiles the subsystem out and would never quit on its own.
#
#      ./SmokePackaged.sh                         4 shots, 120 s in-game budget
#      ./SmokePackaged.sh --shots 6 --timeout 180
#      ./SmokePackaged.sh -- -ResX=1920 -ResY=1080 extra switches for the game
#
#  OB_SMOKE_APP overrides the binary, for a build staged somewhere else.
#  Exit code: 0 on PASS; 1 on FAIL, on a timeout, or when the game quit
#  without a verdict. The verdict is read from the [OB.SMOKE] line: a Mac build
#  exits 0 after a clean shutdown whatever it logged. The full log is kept
#  under Saved/Smoke/.
# ===========================================================================
set -uo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_BIN="${OB_SMOKE_APP:-${GAME_DIR}/Saved/StagedBuilds/Mac/OneBraham.app/Contents/MacOS/OneBraham}"

SHOTS=4
TIMEOUT=120
EXTRA=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --shots)
            SHOTS="${2:?--shots needs a number}"
            shift 2
            ;;
        --timeout)
            TIMEOUT="${2:?--timeout needs a number of seconds}"
            shift 2
            ;;
        --)
            shift
            EXTRA+=("$@")
            break
            ;;
        *)
            EXTRA+=("$1")
            shift
            ;;
    esac
done

case "${SHOTS}${TIMEOUT}" in
    *[!0-9]*)
        echo "  [OB] --shots and --timeout take whole numbers (got '${SHOTS}', '${TIMEOUT}')"
        exit 1
        ;;
esac

# -f as well as -x: a directory passes -x, and OB_SMOKE_APP pointed at the .app
# itself (not Contents/MacOS/OneBraham) would otherwise "start" and log nothing.
if [ ! -f "${APP_BIN}" ] || [ ! -x "${APP_BIN}" ]; then
    echo "  [OB] The staged Mac game was not found at:"
    echo "         ${APP_BIN}"
    echo
    echo "  Package a Development build first (it stages to Saved/StagedBuilds/Mac),"
    echo "  or point OB_SMOKE_APP at the OneBraham binary inside another .app."
    exit 1
fi

LOG_DIR="${GAME_DIR}/Saved/Smoke"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/smoke_$(date +%Y%m%d_%H%M%S).log"

# The script's own deadline: the game's budget covers boot to verdict, and the
# extra minute is for the engine to start and to shut down cleanly after it.
DEADLINE=$((TIMEOUT + 60))

echo
echo "  ONE BRAHAM - packaged smoke test"
echo "  ================================"
echo "  binary   ${APP_BIN}"
echo "  shots    ${SHOTS}, game budget ${TIMEOUT}s, hard limit ${DEADLINE}s"
echo "  log      ${LOG_FILE}"
echo

"${APP_BIN}" -OBSmoke -OBSmokeShots="${SHOTS}" -OBSmokeTimeout="${TIMEOUT}" \
    -windowed -ResX=1280 -ResY=720 -unattended -nosplash \
    -log -stdout -FullStdOutLogOutput \
    ${EXTRA+"${EXTRA[@]}"} >"${LOG_FILE}" 2>&1 &
GAME_PID=$!

ELAPSED=0
TIMED_OUT=0
while kill -0 "${GAME_PID}" 2>/dev/null; do
    if [ "${ELAPSED}" -ge "${DEADLINE}" ]; then
        TIMED_OUT=1
        echo "  [OB] no exit after ${DEADLINE}s - stopping the game"
        kill -TERM "${GAME_PID}" 2>/dev/null
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "${GAME_PID}" 2>/dev/null || break
            sleep 1
        done
        kill -KILL "${GAME_PID}" 2>/dev/null
        break
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done

wait "${GAME_PID}" 2>/dev/null
GAME_CODE=$?

echo "  [OB.SMOKE] lines:"
if ! grep -F "[OB.SMOKE]" "${LOG_FILE}" | sed -e 's/^.*\[OB\.SMOKE\]/    /'; then
    echo "    (none - the subsystem never started; is this a Shipping build, or"
    echo "     did the game die before the game instance came up?)"
fi
echo

# Where the screenshots went: the game logs each absolute path, and the
# sandboxed and unsandboxed Saved folders are listed in case it never got
# that far. A sandboxed build writes inside its container.
echo "  screenshots:"
SHOT_LINES="$(grep -F "[OB.SMOKE] shot " "${LOG_FILE}" | sed -e 's/^.*-> //')"
if [ -n "${SHOT_LINES}" ]; then
    while IFS= read -r shot; do
        if [ -f "${shot}" ]; then
            echo "    ${shot}"
        else
            echo "    ${shot}  (missing)"
        fi
    done <<<"${SHOT_LINES}"
else
    echo "    (none requested)"
fi
for dir in "${HOME}"/Library/Containers/*/Data/Library/Application\ Support/Epic/OneBraham/Saved/Screenshots \
    "${HOME}/Library/Application Support/Epic/OneBraham/Saved/Screenshots"; do
    if [ -d "${dir}" ]; then
        echo "    folder: ${dir}"
    fi
done
echo

if [ "${TIMED_OUT}" -ne 0 ]; then
    echo "  [OB] smoke test TIMED OUT (log: ${LOG_FILE})"
    exit 1
fi

if grep -qF "[OB.SMOKE] FAIL" "${LOG_FILE}"; then
    echo "  [OB] smoke test FAILED (game exit ${GAME_CODE}, log: ${LOG_FILE})"
    exit 1
fi

if ! grep -qF "[OB.SMOKE] PASS" "${LOG_FILE}"; then
    echo "  [OB] the game quit without a verdict (exit ${GAME_CODE}, log: ${LOG_FILE})"
    exit 1
fi

if [ "${GAME_CODE}" -ne 0 ]; then
    echo "  [OB] PASS was logged but the game exited ${GAME_CODE} (log: ${LOG_FILE})"
    exit 1
fi

echo "  [OB] smoke test passed."
exit 0
