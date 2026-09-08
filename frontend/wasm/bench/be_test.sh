#!/bin/bash
# be_test.sh — backend pytest in the lcr conda env.
cd "$HOME/daily/program/ESP32/LCR/backend"
"$HOME/miniconda3/envs/lcr/bin/python" -m pytest -q 2>&1 | tail -4
