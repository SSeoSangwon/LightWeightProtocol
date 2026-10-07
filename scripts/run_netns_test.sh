#!/usr/bin/env bash
set -u

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
RESULT_DIR="$ROOT_DIR/results"
mkdir -p "$RESULT_DIR"

cleanup()
{
    sudo "$ROOT_DIR/scripts/cleanup_netns.sh" >/dev/null 2>&1 || true
}

sudo "$ROOT_DIR/scripts/setup_netns.sh"
SETUP_RC=$?
if [[ "$SETUP_RC" -ne 0 ]]; then
    cleanup
    echo 'LWP_NETNS_TEST=FAIL reason=namespace_setup'
    exit 1
fi

sudo ip netns exec lwp-b "$ROOT_DIR/build/lwp_receiver" \
    "$ROOT_DIR/config/contexts.conf" 100 10.200.1.2 8 \
    > "$RESULT_DIR/receiver.log" 2>&1 &
RECEIVER_PID=$!

sleep 0.2

sudo ip netns exec lwp-a "$ROOT_DIR/build/lwp_sender" \
    "$ROOT_DIR/config/contexts.conf" 100 10.200.1.1 1000 64 8 200 \
    | tee "$RESULT_DIR/sender.log"
SENDER_RC=${PIPESTATUS[0]}

wait "$RECEIVER_PID"
RECEIVER_RC=$?
cat "$RESULT_DIR/receiver.log"

cleanup

if [[ "$SENDER_RC" -eq 0 && "$RECEIVER_RC" -eq 0 ]] && \
   grep -q 'LWP_RESULT messages=1000' "$RESULT_DIR/sender.log" && \
   grep -q 'LWP_RECEIVER_RESULT delivered_messages=1000' "$RESULT_DIR/receiver.log"; then
    echo 'LWP_NETNS_TEST=PASS'
    exit 0
fi

echo "LWP_NETNS_TEST=FAIL sender_rc=$SENDER_RC receiver_rc=$RECEIVER_RC"
exit 1
