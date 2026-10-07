#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

MESSAGES="${1:-2000}"
REPEATS="${2:-3}"
SIZES=(8 16 32 64 128 256 512)
TCP_PORT=45100
UDP_PORT=45101
OUT="results/short_message_benchmark.csv"
TMP="results/.shortmsg_tmp"

mkdir -p "$TMP"
rm -f "$TMP"/*

extract()
{
    local file="$1" key="$2"
    awk -v key="$key" '{for(i=1;i<=NF;i++){split($i,a,"="); if(a[1]==key){print a[2]; exit}}}' "$file"
}

cleanup()
{
    sudo "$ROOT/scripts/cleanup_netns.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

if ! [[ "$MESSAGES" =~ ^[0-9]+$ ]] || [ "$MESSAGES" -lt 1 ] || [ "$MESSAGES" -gt 60000 ]; then
    echo "Invalid message count: $MESSAGES" >&2
    exit 1
fi
if ! [[ "$REPEATS" =~ ^[0-9]+$ ]] || [ "$REPEATS" -lt 1 ] || [ "$REPEATS" -gt 20 ]; then
    echo "Invalid repeat count: $REPEATS" >&2
    exit 1
fi

make >/dev/null || exit 1
sudo "$ROOT/scripts/cleanup_netns.sh" >/dev/null 2>&1 || true
sudo "$ROOT/scripts/setup_netns.sh" || exit 1

if command -v ethtool >/dev/null 2>&1; then
    sudo ip netns exec lwp-a ethtool -K lwp-veth-a tso off gso off gro off >/dev/null 2>&1 || true
    sudo ip netns exec lwp-b ethtool -K lwp-veth-b tso off gso off gro off >/dev/null 2>&1 || true
fi

cat > "$OUT" <<'CSV'
protocol,payload_bytes,messages,repeat,useful_bytes,elapsed_us,goodput_mbps,nominal_transport_header_bytes,nominal_ipv4_plus_transport_bytes,nominal_data_header_ratio,captured_packets,captured_ipv4_bytes,captured_l2_bytes,a2b_packets,a2b_ipv4_bytes,b2a_packets,b2a_ipv4_bytes,observed_ipv4_overhead_bytes,observed_ipv4_overhead_ratio,delivered_messages,delivery_ok,retransmissions,timeouts,duplicate_or_out_of_order
CSV

run_case()
{
    local proto="$1" size="$2" rep="$3"
    local sender_log="$TMP/${proto}_${size}_${rep}_sender.log"
    local receiver_log="$TMP/${proto}_${size}_${rep}_receiver.log"
    local meter_a_log="$TMP/${proto}_${size}_${rep}_meter_a.log"
    local meter_b_log="$TMP/${proto}_${size}_${rep}_meter_b.log"
    local meter_a_pid="" meter_b_pid receiver_pid sender_rc receiver_rc meter_a_rc=0 meter_b_rc=0
    local useful elapsed goodput captured_packets captured_ipv4 captured_l2 overhead overhead_ratio
    local delivered delivery_ok retrans timeouts dup nominal_transport nominal_iptransport nominal_ratio
    local a2b_packets a2b_ipv4 a2b_l2 b2a_packets b2a_ipv4 b2a_l2
    local meter_args

    rm -f "$sender_log" "$receiver_log" "$meter_a_log" "$meter_b_log"

    case "$proto" in
        lwp)
            nominal_transport=11
            meter_args="lwp 10.200.1.1 10.200.1.2 0 250"
            ;;
        tcp)
            nominal_transport=20
            meter_args="tcp 10.200.1.1 10.200.1.2 $TCP_PORT 250"
            ;;
        udp)
            nominal_transport=8
            meter_args="udp 10.200.1.1 10.200.1.2 $UDP_PORT 250"
            ;;
        *)
            echo "Unknown protocol: $proto" >&2
            return 1
            ;;
    esac

    # veth packet sockets in this setup reliably observe ingress.  Therefore:
    #   meter in lwp-b measures A->B traffic
    #   meter in lwp-a measures B->A traffic (needed for reliable transports)
    sudo ip netns exec lwp-b ./build/packet_meter lwp-veth-b $meter_args >"$meter_b_log" 2>&1 &
    meter_b_pid=$!

    if [ "$proto" != "udp" ]; then
        sudo ip netns exec lwp-a ./build/packet_meter lwp-veth-a $meter_args >"$meter_a_log" 2>&1 &
        meter_a_pid=$!
    fi

    case "$proto" in
        lwp)
            sudo ip netns exec lwp-b ./build/lwp_receiver ./config/contexts.conf 100 10.200.1.2 8 >"$receiver_log" 2>&1 &
            receiver_pid=$!
            sleep 0.08
            sudo ip netns exec lwp-a ./build/lwp_sender ./config/contexts.conf 100 10.200.1.1 "$MESSAGES" "$size" 8 200 >"$sender_log" 2>&1
            sender_rc=$?
            wait "$receiver_pid"; receiver_rc=$?
            elapsed=$(extract "$sender_log" elapsed_ms)
            elapsed=$((elapsed * 1000))
            goodput=$(extract "$sender_log" goodput_mbps)
            delivered=$(extract "$receiver_log" delivered_messages)
            retrans=$(extract "$sender_log" retransmissions)
            timeouts=$(extract "$sender_log" timeouts)
            dup=$(extract "$receiver_log" duplicate_or_out_of_order)
            ;;
        tcp)
            sudo ip netns exec lwp-b ./build/tcp_receiver 10.200.1.2 "$TCP_PORT" "$MESSAGES" "$size" >"$receiver_log" 2>&1 &
            receiver_pid=$!
            sleep 0.08
            sudo ip netns exec lwp-a ./build/tcp_sender 10.200.1.1 10.200.1.2 "$TCP_PORT" "$MESSAGES" "$size" >"$sender_log" 2>&1
            sender_rc=$?
            wait "$receiver_pid"; receiver_rc=$?
            elapsed=$(extract "$sender_log" elapsed_us)
            goodput=$(extract "$sender_log" goodput_mbps)
            delivered=$(extract "$receiver_log" delivered_messages)
            retrans=0
            timeouts=0
            dup=0
            ;;
        udp)
            sudo ip netns exec lwp-b ./build/udp_receiver 10.200.1.2 "$UDP_PORT" "$MESSAGES" "$size" >"$receiver_log" 2>&1 &
            receiver_pid=$!
            sleep 0.08
            sudo ip netns exec lwp-a ./build/udp_sender 10.200.1.1 10.200.1.2 "$UDP_PORT" "$MESSAGES" "$size" >"$sender_log" 2>&1
            sender_rc=$?
            wait "$receiver_pid"; receiver_rc=$?
            elapsed=$(extract "$sender_log" elapsed_us)
            goodput=$(extract "$sender_log" goodput_mbps)
            delivered=$(extract "$receiver_log" delivered_messages)
            retrans=0
            timeouts=0
            dup=$(extract "$receiver_log" out_of_order)
            ;;
    esac

    wait "$meter_b_pid"; meter_b_rc=$?
    if [ -n "$meter_a_pid" ]; then
        wait "$meter_a_pid"; meter_a_rc=$?
    fi

    # A->B is captured as ingress on lwp-b.
    a2b_packets=$(extract "$meter_b_log" a2b_packets)
    a2b_ipv4=$(extract "$meter_b_log" a2b_ipv4_bytes)
    a2b_l2=$(extract "$meter_b_log" a2b_l2_bytes)

    if [ "$proto" = "udp" ]; then
        b2a_packets=0
        b2a_ipv4=0
        b2a_l2=0
    else
        # B->A is captured as ingress on lwp-a.
        b2a_packets=$(extract "$meter_a_log" b2a_packets)
        b2a_ipv4=$(extract "$meter_a_log" b2a_ipv4_bytes)
        b2a_l2=$(extract "$meter_a_log" b2a_l2_bytes)
    fi

    if [ -z "$a2b_ipv4" ] || { [ "$proto" != "udp" ] && [ -z "$b2a_ipv4" ]; }; then
        echo "METER_FAIL protocol=$proto size=$size repeat=$rep" >&2
        echo "--- meter A (B->A) ---" >&2; [ -f "$meter_a_log" ] && cat "$meter_a_log" >&2
        echo "--- meter B (A->B) ---" >&2; cat "$meter_b_log" >&2
        return 1
    fi

    useful=$((MESSAGES * size))
    captured_packets=$((a2b_packets + b2a_packets))
    captured_ipv4=$((a2b_ipv4 + b2a_ipv4))
    captured_l2=$((a2b_l2 + b2a_l2))
    overhead=$((captured_ipv4 - useful))
    if [ "$overhead" -lt 0 ]; then overhead=0; fi
    overhead_ratio=$(awk -v o="$overhead" -v u="$useful" 'BEGIN{if(u==0)print "0"; else printf "%.6f", o/u}')
    nominal_iptransport=$((20 + nominal_transport))
    nominal_ratio=$(awk -v h="$nominal_iptransport" -v p="$size" 'BEGIN{printf "%.6f", h/(h+p)}')

    delivery_ok=0
    if [ "$sender_rc" -eq 0 ] && [ "$receiver_rc" -eq 0 ] && [ "$meter_b_rc" -eq 0 ] &&        { [ "$proto" = "udp" ] || [ "$meter_a_rc" -eq 0 ]; } && [ "$delivered" = "$MESSAGES" ]; then
        delivery_ok=1
    fi

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s
'         "$proto" "$size" "$MESSAGES" "$rep" "$useful" "$elapsed" "$goodput"         "$nominal_transport" "$nominal_iptransport" "$nominal_ratio"         "$captured_packets" "$captured_ipv4" "$captured_l2"         "$a2b_packets" "$a2b_ipv4" "$b2a_packets" "$b2a_ipv4"         "$overhead" "$overhead_ratio" "$delivered" "$delivery_ok"         "$retrans" "$timeouts" "$dup" >> "$OUT"

    echo "SHORTMSG_CASE protocol=$proto payload=$size repeat=$rep delivery=$delivery_ok a2b_ipv4=$a2b_ipv4 b2a_ipv4=$b2a_ipv4 total_ipv4=$captured_ipv4 overhead_ratio=$overhead_ratio goodput_mbps=$goodput"
    if [ "$delivery_ok" -ne 1 ]; then
        echo "--- sender ---" >&2; cat "$sender_log" >&2
        echo "--- receiver ---" >&2; cat "$receiver_log" >&2
        echo "--- meter A (B->A) ---" >&2; [ -f "$meter_a_log" ] && cat "$meter_a_log" >&2
        echo "--- meter B (A->B) ---" >&2; cat "$meter_b_log" >&2
        return 1
    fi
}

for rep in $(seq 1 "$REPEATS"); do
    for size in "${SIZES[@]}"; do
        run_case lwp "$size" "$rep" || exit 1
        run_case tcp "$size" "$rep" || exit 1
        run_case udp "$size" "$rep" || exit 1
    done
done

python3 "$ROOT/scripts/summarize_short_message.py" "$OUT" || exit 1
echo "LWP_SHORT_MESSAGE_BENCHMARK=PASS csv=$OUT"

