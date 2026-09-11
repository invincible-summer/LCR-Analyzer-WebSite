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

## Attention

不要修改任何带有DO_NOT_TOUCH前缀的文件！这些文件强依赖硬件结构，修改会导致无法预知的错误！
如果DO_NOT_TOUCH_lcr_api.h中的API满足格式要求，任何有关产生频率、测量阻抗、ADC调用（任何DO_NOT_TOUCH_lcr_api.h中写了的）等的操作请直接使用DO_NOT_TOUCH_lcr_api.h中的API，不要自己杜撰API或使用任何不在此文件中的API!
如果DO_NOT_TOUCH_lcr_api.h中的API不满足某些标准代码格式要求，你可以新建文件把里面的API再封装产生更好的API，或在保持核心逻辑完全不变的情况下重写接口部分API，但是请不要更改DO_NOT_TOUCH_lcr_api.h，你应该新建lcr_api.h并在其中使用中文详细解释你做的更改！
带有DO_NOT_TOUCH前缀的文件中的工作方式都在硬件中验证可行，不要修改。
DO_NOT_TOUCH_EXAMPLE里是API调用示例，可以参考，但是不要修改此EXAMPLE，如果你封装了新的API，可以新建EXAMPLE。
硬件条件（请务必了解，任何杜撰的硬件都会直接导致无法工作）：使用LCD_CAM在8个GPIO上生成正弦波对应的并行信号，并通过外部已有的电阻网络形成DAC。3个GPIO通过74HC595连接10个控制端，4个控制TIA放大倍数，2个控制电压放大倍数，2个控制电流放大倍数，两个控制是否双端口
信号经过外部详细处理之后会送到两个ADC引脚进行间隔采样，并经过计算和校准输出数据。
请注意，任何尝试使用外部DAC芯片的代码均无法工作，本项目不包含除74HC595外任何外部数字芯片。
附录：带有DO_NOT_TOUCH前缀的文件列表(你不能修改这些文件，你应仅使用DO_NOT_TOUCH_lcr_api.h中的API或如上所述新建文件并少许修改。)：
DO_NOT_TOUCH_freq_calc.h
DO_NOT_TOUCH_hong.h（本文件没有在任何地方被引用，请不要引用该文件，该文件仅作为将来可能使用的预留）
DO_NOT_TOUCH_lcr_adc.h
DO_NOT_TOUCH_lcr_api.h
DO_NOT_TOUCH_lcr_calib_core.h
DO_NOT_TOUCH_lcr_calib.h
DO_NOT_TOUCH_lcr_diag.h
DO_NOT_TOUCH_lcr_measure.h
DO_NOT_TOUCH_lcr_tone.h
DO_NOT_TOUCH_sinwave.h
DO_NOT_TOUCH_EXAMPLE.ino.example