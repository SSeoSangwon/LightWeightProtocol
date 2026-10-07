#!/usr/bin/env bash
set -u

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
RESULT_DIR="$ROOT_DIR/results/loss_sweep"
CSV="$RESULT_DIR/loss_sweep.csv"
SUMMARY="$RESULT_DIR/loss_sweep_summary.txt"
RECEIVER_PID=""

MESSAGES=${1:-1000}
PAYLOAD_BYTES=${2:-64}
REPEATS=${3:-3}
WINDOW=${4:-8}
RTO_MS=${5:-100}
LOSSES=(0 0.1 1 5)

cleanup()
{
    sudo "$ROOT_DIR/scripts/cleanup_netns.sh" >/dev/null 2>&1 || true
}

finish()
{
    cleanup
}

trap finish EXIT INT TERM

if [[ ! "$MESSAGES" =~ ^[0-9]+$ || "$MESSAGES" -le 0 ]]; then
    echo 'LWP_LOSS_SWEEP=FAIL reason=invalid_messages'
    exit 1
fi
if [[ ! "$PAYLOAD_BYTES" =~ ^[0-9]+$ || "$PAYLOAD_BYTES" -le 0 ]]; then
    echo 'LWP_LOSS_SWEEP=FAIL reason=invalid_payload'
    exit 1
fi
if [[ ! "$REPEATS" =~ ^[0-9]+$ || "$REPEATS" -le 0 ]]; then
    echo 'LWP_LOSS_SWEEP=FAIL reason=invalid_repeats'
    exit 1
fi
if [[ ! "$WINDOW" =~ ^[0-9]+$ || "$WINDOW" -le 0 ]]; then
    echo 'LWP_LOSS_SWEEP=FAIL reason=invalid_window'
    exit 1
fi
if [[ ! "$RTO_MS" =~ ^[0-9]+$ || "$RTO_MS" -le 0 ]]; then
    echo 'LWP_LOSS_SWEEP=FAIL reason=invalid_rto'
    exit 1
fi

for cmd in tc timeout python3; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "LWP_LOSS_SWEEP=FAIL reason=missing_${cmd}"
        exit 1
    fi
done

if [[ ! -x "$ROOT_DIR/build/lwp_sender" || ! -x "$ROOT_DIR/build/lwp_receiver" ]]; then
    echo 'LWP_LOSS_SWEEP=FAIL reason=missing_build'
    echo 'Run: make && make test'
    exit 1
fi

mkdir -p "$RESULT_DIR"
rm -f "$CSV" "$SUMMARY"
printf '%s\n' 'loss_percent,repeat,messages,payload_bytes,window,rto_ms,delivered,original_tx,ack_requests,retransmissions,timeouts,duplicate_or_out_of_order,acknowledgments_sent,corrupted,elapsed_ms,goodput_mbps,sender_rc,receiver_rc' > "$CSV"

cleanup
sudo "$ROOT_DIR/scripts/setup_netns.sh"
if [[ $? -ne 0 ]]; then
    echo 'LWP_LOSS_SWEEP=FAIL reason=namespace_setup'
    exit 1
fi

echo "LWP_LOSS_SWEEP_CONFIG messages=$MESSAGES payload_bytes=$PAYLOAD_BYTES repeats=$REPEATS window=$WINDOW rto_ms=$RTO_MS losses=0,0.1,1,5"

extract_metric()
{
    local line="$1"
    local key="$2"
    printf '%s\n' "$line" | sed -n "s/.*${key}=\([^ ]*\).*/\1/p"
}

TOTAL_RUNS=0
PASS_RUNS=0

for loss in "${LOSSES[@]}"; do
    for ((repeat=1; repeat<=REPEATS; repeat++)); do
        TOTAL_RUNS=$((TOTAL_RUNS + 1))
        TAG="loss${loss//./p}_r${repeat}"
        SENDER_LOG="$RESULT_DIR/sender_${TAG}.log"
        RECEIVER_LOG="$RESULT_DIR/receiver_${TAG}.log"
        rm -f "$SENDER_LOG" "$RECEIVER_LOG"

        sudo ip netns exec lwp-a tc qdisc replace dev lwp-veth-a root netem loss "${loss}%"
        if [[ $? -ne 0 ]]; then
            echo "LWP_LOSS_SWEEP=FAIL reason=netem_setup loss=$loss repeat=$repeat"
            exit 1
        fi

        timeout 30s sudo ip netns exec lwp-b \
            "$ROOT_DIR/build/lwp_receiver" \
            "$ROOT_DIR/config/contexts.conf" \
            100 \
            10.200.1.2 \
            "$WINDOW" \
            > "$RECEIVER_LOG" 2>&1 &
        RECEIVER_PID=$!
        sleep 0.15

        sudo ip netns exec lwp-a \
            "$ROOT_DIR/build/lwp_sender" \
            "$ROOT_DIR/config/contexts.conf" \
            100 \
            10.200.1.1 \
            "$MESSAGES" \
            "$PAYLOAD_BYTES" \
            "$WINDOW" \
            "$RTO_MS" \
            > "$SENDER_LOG" 2>&1
        SENDER_RC=$?

        wait "$RECEIVER_PID"
        RECEIVER_RC=$?
        RECEIVER_PID=""

        SENDER_RESULT=$(grep 'LWP_RESULT ' "$SENDER_LOG" | tail -n 1 || true)
        RECEIVER_RESULT=$(grep 'LWP_RECEIVER_RESULT ' "$RECEIVER_LOG" | tail -n 1 || true)

        if [[ -z "$SENDER_RESULT" || -z "$RECEIVER_RESULT" ]]; then
            echo "LOSS_SWEEP_CASE loss=$loss repeat=$repeat status=FAIL reason=missing_result"
            exit 1
        fi

        SENT_MESSAGES=$(extract_metric "$SENDER_RESULT" messages)
        ORIGINAL_TX=$(extract_metric "$SENDER_RESULT" original_tx)
        ACK_REQUESTS=$(extract_metric "$SENDER_RESULT" ack_requests)
        RETRANSMISSIONS=$(extract_metric "$SENDER_RESULT" retransmissions)
        TIMEOUTS=$(extract_metric "$SENDER_RESULT" timeouts)
        ELAPSED_MS=$(extract_metric "$SENDER_RESULT" elapsed_ms)
        GOODPUT=$(extract_metric "$SENDER_RESULT" goodput_mbps)

        DELIVERED=$(extract_metric "$RECEIVER_RESULT" delivered_messages)
        DUP_OOO=$(extract_metric "$RECEIVER_RESULT" duplicate_or_out_of_order)
        CORRUPTED=$(extract_metric "$RECEIVER_RESULT" corrupted)
        ACKS_SENT=$(extract_metric "$RECEIVER_RESULT" acknowledgments_sent)

        STATUS=PASS
        if [[ "$SENDER_RC" -ne 0 || "$RECEIVER_RC" -ne 0 || \
              "$SENT_MESSAGES" != "$MESSAGES" || \
              "$ORIGINAL_TX" != "$MESSAGES" || \
              "$DELIVERED" != "$MESSAGES" || \
              "$CORRUPTED" != "0" || \
              -z "$ACK_REQUESTS" || "$ACK_REQUESTS" -ge "$MESSAGES" ]]; then
            STATUS=FAIL
        fi

        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "$loss" "$repeat" "$MESSAGES" "$PAYLOAD_BYTES" "$WINDOW" "$RTO_MS" \
            "$DELIVERED" "$ORIGINAL_TX" "$ACK_REQUESTS" "$RETRANSMISSIONS" "$TIMEOUTS" \
            "$DUP_OOO" "$ACKS_SENT" "$CORRUPTED" "$ELAPSED_MS" "$GOODPUT" \
            "$SENDER_RC" "$RECEIVER_RC" >> "$CSV"

        echo "LOSS_SWEEP_CASE loss=$loss repeat=$repeat status=$STATUS delivered=$DELIVERED retransmissions=$RETRANSMISSIONS timeouts=$TIMEOUTS ack_requests=$ACK_REQUESTS acknowledgments_sent=$ACKS_SENT goodput_mbps=$GOODPUT"

        if [[ "$STATUS" != "PASS" ]]; then
            echo "LWP_LOSS_SWEEP=FAIL reason=case_validation loss=$loss repeat=$repeat"
            exit 1
        fi
        PASS_RUNS=$((PASS_RUNS + 1))
    done
done

python3 "$ROOT_DIR/scripts/summarize_loss_sweep.py" "$CSV" | tee "$SUMMARY"
SUMMARY_RC=${PIPESTATUS[0]}
if [[ "$SUMMARY_RC" -ne 0 ]]; then
    echo 'LWP_LOSS_SWEEP=FAIL reason=summary'
    exit 1
fi

if [[ "$PASS_RUNS" -ne "$TOTAL_RUNS" ]]; then
    echo "LWP_LOSS_SWEEP=FAIL reason=run_count pass=$PASS_RUNS total=$TOTAL_RUNS"
    exit 1
fi

echo "LWP_LOSS_SWEEP=PASS runs=$PASS_RUNS csv=results/loss_sweep/loss_sweep.csv"
exit 0
