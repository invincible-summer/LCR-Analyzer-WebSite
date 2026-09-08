#!/bin/bash
# fe_test.sh — run frontend vitest + typecheck build (pnpm via nvm).
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"
cd "$HOME/daily/program/ESP32/LCR/frontend"
pnpm test 2>&1 | tail -6
echo "=== build ==="
pnpm build 2>&1 | tail -4
