#!/usr/bin/env bash
set -euo pipefail
mod="${1:?usage: $0 <module-name>}"
echo function_graph | sudo tee /sys/kernel/debug/tracing/current_tracer >/dev/null
echo ":mod:$mod" | sudo tee /sys/kernel/debug/tracing/set_ftrace_filter >/dev/null
sudo sh -c 'echo 1 > /sys/kernel/debug/tracing/tracing_on'
sudo tail -f /sys/kernel/debug/tracing/trace_pipe
