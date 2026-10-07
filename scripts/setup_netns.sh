#!/usr/bin/env bash
set -u

NS_A=lwp-a
NS_B=lwp-b
VETH_A=lwp-veth-a
VETH_B=lwp-veth-b

fail()
{
    echo "LWP_NETNS_SETUP=FAIL step=$1" >&2
    exit 1
}

ip netns del "$NS_A" 2>/dev/null || true
ip netns del "$NS_B" 2>/dev/null || true

ip netns add "$NS_A" || fail netns_add_a
ip netns add "$NS_B" || fail netns_add_b
ip link add "$VETH_A" type veth peer name "$VETH_B" || fail veth_create
ip link set "$VETH_A" netns "$NS_A" || fail veth_move_a
ip link set "$VETH_B" netns "$NS_B" || fail veth_move_b

ip -n "$NS_A" addr add 10.200.1.1/24 dev "$VETH_A" || fail addr_a
ip -n "$NS_B" addr add 10.200.1.2/24 dev "$VETH_B" || fail addr_b
ip -n "$NS_A" link set lo up || fail lo_a
ip -n "$NS_B" link set lo up || fail lo_b
ip -n "$NS_A" link set "$VETH_A" up || fail link_a
ip -n "$NS_B" link set "$VETH_B" up || fail link_b

printf 'LWP_NETNS_SETUP=PASS\n'
ip -n "$NS_A" -br addr show "$VETH_A"
ip -n "$NS_B" -br addr show "$VETH_B"
