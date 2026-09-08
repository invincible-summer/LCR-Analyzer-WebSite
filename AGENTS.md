# Working on LCR

## Purpose
Build a trustworthy, useful LCR measurement and circuit identification tool. Prefer clear mathematics, working end-to-end features, and evidence over preserving historical implementation choices. Use initiative: investigate, prototype, simplify, and revise architecture when that improves the result. Explain consequential tradeoffs and update the relevant documentation.

## Project map
- `AlgorithmLcr/`: C++17 numerical core shared by the native CLI and browser WASM; vendored Eigen 3.4.0.
- `frontend/`: Vue/TypeScript measurement and identification interface.
- `backend/`: Python measurement service and simulator; inverse algorithms belong in the shared C++ core.
- `ino/`: device firmware.
- `DESIGN.md`: current architecture. Algorithm theory and format contracts live in `AlgorithmLcr/`.

These documents describe the current system, not immutable design constraints. Keep them aligned with deliberate changes. Preserve compatible measurement and circuit data formats where practical; document necessary changes explicitly.

## Engineering judgment
Own the outcome from implementation through verification. Choose tools, experiments, and refactors suited to the problem. Avoid unnecessary approval stops and ceremonial process. Ask when an unresolved product decision materially changes the result; otherwise make a reasonable choice and explain it.

Keep mathematical claims honest: distinguish finite enumeration completeness, local optimization, numerical reliability, and physical identifiability. Expose failures and partial results clearly. Prefer shared implementations over divergent copies of numerical code.

Protect user measurements and unrelated workspace changes. Database changes should preserve historical data. Keep UI language and presentation coherent with the existing Chinese scientific instrument interface, while improving usability when useful.

## Verification
Run checks that exercise the changed behavior. Numerical changes warrant native tests and real measurement benchmarks; browser integration warrants actual WASM execution as well as frontend tests and build. Broaden testing when evidence warrants it, rather than mechanically repeating expensive suites.

Useful commands:
```sh
cmake -S AlgorithmLcr -B /tmp/lcr-v4-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/lcr-v4-build -j2
ctest --test-dir /tmp/lcr-v4-build --output-on-failure
/tmp/lcr-v4-build/lcr_bench random 40 21
# frontend/
pnpm build:wasm # when changing the shared C++ core or WASM bindings
pnpm test
pnpm build
# backend/
conda run -n lcr python -m pytest
# root
./start.sh
./start.sh stop
```

Repository: github.com/invincible-summer/LCR-Analyzer-WebSite. Current development branch: `dev`. If a commit is requested, commit on the development branch without a PR. Do not overwrite unrelated user work.
