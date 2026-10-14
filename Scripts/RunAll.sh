#!/usr/bin/env bash
# ===========================================================================
#  ONE BRAHAM - every check that needs no engine (Linux / macOS host)
# ---------------------------------------------------------------------------
#      ./RunAll.sh                    the engine-free steps, then the validator
#      ./RunAll.sh --only calls       pass switches straight to run_all.py
#      ./RunAll.sh --config-only      the validator's config-only pass
#
#  Same contract as RunAll.bat: nothing but Python 3, and an exit code that
#  means "the project is consistent" rather than "there is an engine here".
#  With no editor installed the content steps report SKIPPED and the exit code
#  stays 0 when nothing failed.
# ===========================================================================
set -uo pipefail

GAME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY_ROOT="${GAME_DIR}/Tools/Python"

PYTHON=""
for candidate in python3 python; do
    if command -v "${candidate}" >/dev/null 2>&1 &&
        "${candidate}" -c 'import sys; raise SystemExit(0 if sys.version_info[0] == 3 else 1)' >/dev/null 2>&1; then
        PYTHON="${candidate}"
        break
    fi
done

if [ -z "${PYTHON}" ]; then
    echo
    echo "  [OB] No Python 3 found on the PATH."
    echo
    echo "  The engine-free checks are Python programs. Install Python 3.11 or"
    echo "  newer, or run Scripts/Setup.bat on a Windows host, which checks for it."
    echo
    exit 1
fi

# Split --config-only out of the arguments: it belongs to the validator.
CONFIG_ONLY=()
PASSTHROUGH=()
for arg in "$@"; do
    if [ "${arg}" = "--config-only" ]; then
        CONFIG_ONLY+=("${arg}")
    else
        PASSTHROUGH+=("${arg}")
    fi
done

echo
echo "  ONE BRAHAM - engine-free checks"
echo "  =============================="
echo

export PYTHONPATH="${PY_ROOT}"

"${PYTHON}" "${PY_ROOT}/onebraham/run_all.py" ${PASSTHROUGH+"${PASSTHROUGH[@]}"}
RUN_ALL_CODE=$?

echo
"${PYTHON}" "${PY_ROOT}/onebraham/validate_project.py" ${CONFIG_ONLY+"${CONFIG_ONLY[@]}"}
VALIDATE_CODE=$?

echo
if [ "${RUN_ALL_CODE}" -ne 0 ]; then
    echo "  [OB] build checks FAILED (exit ${RUN_ALL_CODE})"
    exit "${RUN_ALL_CODE}"
fi
if [ "${VALIDATE_CODE}" -ne 0 ]; then
    echo "  [OB] validator FAILED (exit ${VALIDATE_CODE})"
    exit "${VALIDATE_CODE}"
fi

echo "  [OB] all engine-free checks passed."
exit 0
