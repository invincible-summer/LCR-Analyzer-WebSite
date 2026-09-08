#!/bin/bash
# sweep_gate.sh — kRhoGate sweep on independent seeds (R13/R14).
# usage: bash sweep_gate.sh  (run from frontend/wasm)
set -e
SEL="../../AlgorithmLcr/Try1-Completely unknown single port fitting/cppversion/src/selector.cpp"
for G in 1.1 1.2 1.3 1.4; do
  sed -i "s|^constexpr double kRhoGate = .*|constexpr double kRhoGate = $G;|" "$SEL"
  grep -q "kRhoGate = $G;" "$SEL" || { echo "SED FAILED for $G"; exit 1; }
  cmake --build build-native -j 8 > /dev/null 2>&1
  echo "===== gate=$G ====="
  python3 bench/realfam.py --draws 40 --seed 21 --jobs 14 --tag "gate$G" 2>&1 | tail -6
  python3 bench/suite2.py run --n 150 --seed 12 --jobs 14 --tag "gate$G" 2>&1 | grep "try1:"
  python3 bench/suite.py run --n 200 --seed 2 --jobs 14 --tag "gate$G" 2>&1 | grep "try1:"
  python3 bench/real4.py 2>&1 | tail -1
done
