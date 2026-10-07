#!/usr/bin/env bash
set -u

missing=0
for cmd in gcc make ip; do
    if command -v "$cmd" >/dev/null 2>&1; then
        printf 'CHECK %-8s PASS (%s)\n' "$cmd" "$(command -v "$cmd")"
    else
        printf 'CHECK %-8s FAIL\n' "$cmd"
        missing=1
    fi
done

if command -v sudo >/dev/null 2>&1; then
    echo 'CHECK sudo     PASS'
else
    echo 'CHECK sudo     FAIL'
    missing=1
fi

if [[ "$missing" -ne 0 ]]; then
    echo 'LWP_ENV_CHECK=FAIL'
    exit 1
fi

echo 'LWP_ENV_CHECK=PASS'
echo 'NOTE: raw sockets require root/CAP_NET_RAW; network namespaces require root/CAP_NET_ADMIN.'
