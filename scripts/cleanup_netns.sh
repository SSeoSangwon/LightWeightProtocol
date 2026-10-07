#!/usr/bin/env bash
set -u

ip netns del lwp-a 2>/dev/null || true
ip netns del lwp-b 2>/dev/null || true
printf 'LWP_NETNS_CLEANUP=PASS\n'
