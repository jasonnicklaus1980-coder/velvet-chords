#!/bin/sh
# CI helper: run a command; if it fails, publish its last lines as a GitHub annotation
# (annotations are visible on phones, where full logs are hard to reach).
# usage: sh scripts/ci_run.sh "Step title" command [args...]
title="$1"; shift
log=$(mktemp)
"$@" >"$log" 2>&1
rc=$?
cat "$log"
if [ "$rc" -ne 0 ]; then
  tail_text=$(tail -n 15 "$log" | sed -e 's/%/%25/g' -e 's/\r//g' | awk 'BEGIN{ORS="%0A"} {print}')
  echo "::error title=${title} failed (exit ${rc})::${tail_text}"
fi
rm -f "$log"
exit "$rc"
