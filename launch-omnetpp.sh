#!/usr/bin/env bash
# Launch the OMNeT++ IDE with the omnetpp-6.3.0 + inet-4.5.4 environment.
#
# Usage:  ./launch-omnetpp.sh
#
# Uses `opp_env run -c "omnetpp"` so PATH / OMNETPP_ROOT / INET_ROOT are set
# up exactly like in an interactive `opp_env shell` session. The IDE is the
# foreground process; close it normally to end the script.

set -euo pipefail

OPP_ENV=/home/opp_env/.venv/bin/opp_env
WORKSPACE=/home/opp_env/default_workspace

if [[ ! -x "$OPP_ENV" ]]; then
    echo "ERROR: opp_env not found at $OPP_ENV" >&2
    exit 1
fi

cd "$WORKSPACE"

exec "$OPP_ENV" run omnetpp-6.3.0 inet-4.5.4 --no-isolated -c "omnetpp"
