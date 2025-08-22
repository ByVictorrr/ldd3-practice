#!/usr/bin/env bash
set -euo pipefail
sudo sh -c 'echo 0 > /sys/kernel/debug/tracing/tracing_on'
