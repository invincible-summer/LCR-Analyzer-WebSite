#!/bin/bash
# Dump impedance data for every failing sweep case listed in a cases file.
# usage: dump_failing_cases.sh <cases.txt> <outdir>
set -e
CASES=$1
OUT=$2
mkdir -p "$OUT"
while IFS= read -r line; do
    [ -z "$line" ] && continue
    cs=$(echo "$line" | sed 's/#.*//')
    draw=$(echo "$line" | sed 's/.*#//')
    safe=$(echo "$cs" | tr '/' '_')
    /tmp/dump_case "$cs:$draw" "$OUT/${safe}_${draw}.txt" > "$OUT/${safe}_${draw}.info" 2>&1
done < "$CASES"
ls "$OUT"/*.txt | wc -l
