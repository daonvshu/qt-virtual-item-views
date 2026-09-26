# CI 接入（未提交 workflow，配置留在这里）

仓库里**没有** `.github/workflows/`：本项目的所有验证（四种 Qt kit × 库形态组合的
configure → 构建 → CTest → 示例 → 基准不变量 → 安装 + 消费端冒烟）都在
`scripts/validate.ps1` 里跑过一遍，而"提交一个没在任何 runner 上跑过的 workflow"比不提交更糟
—— 一个绿的 CI 徽章只有在它真的跑过之后才有意义。所以这里留下**可以直接复制**的配置与踩过的
环境坑，拿到 runner（或 GitHub 账号）时把 YAML 存成 `.github/workflows/ci.yml` 即可。

> 本地已实测的是 **Windows**：MSVC 19.50 + Qt 5.15.2 / 6.11.2 × 静态 / 动态四种组合
> （28 个 CTest 目标 + 12 个示例 + 消费端冒烟全绿），MinGW 的 **GCC 13.1 / GCC 8.1 /
> llvm-mingw Clang 17.0.6** 三个 kit（Debug + Release），以及 **MSVC ASan** 与
> **llvm-mingw UBSan**（见 §5 与 [abi.md](abi.md) §5 的支持矩阵）。
> **Linux / macOS 未实测** —— 那需要一台 runner；下表里的 `ubuntu-*` job 就是为此准备的。

## 1. 该跑的 job

| job | 目的 | 备注 |
| --- | --- | --- |
| `windows-msvc-qt6` | 主平台回归 | `scripts/validate.ps1` 一把梭（含库形态 × 安装消费端） |
| `windows-msvc-release` | Release 构建 + Release 基准（审查要求的 "Release benchmark smoke"） | `scripts/validate.ps1 -Release -Library Static`，本机已实跑：两个 Qt 版本 14 步全绿 |
| `windows-mingw-gcc` / `-clang` | Windows 上的 GCC / Clang（审查要求的 GNU 工具链覆盖） | `scripts/validate.ps1 -MinGW -Library Static`，本机已实跑：三个 kit（GCC 13.1 / Clang 17.0.6 / GCC 8.1）Debug + Release 各 21 步全绿 |
| `ubuntu-gcc-qt6` | 开源常见组合 | Qt 6 走 apt（`qt6-base-dev`），只有 Core/Gui/Widgets/Test |
| `ubuntu-gcc-qt6-asan` | ASan + UBSan（Linux 侧） | `-fsanitize=address,undefined`，Debug。**两者的 Windows 版本本机都已实跑**：ASan 走 MSVC（`-Asan`），UBSan 走 llvm-mingw 的 Clang（`-UBSan`），见 §5 |
| `ubuntu-gcc-qt5` | 老版本回归 | 只在 runner 能稳定拿到 Qt 5.15 时加；拿不到就先不写 |

## 2. 可直接复制的 workflow

```yaml
name: ci

on:
  push:
  pull_request:

jobs:
  windows-msvc:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: jurplel/install-qt-action@v4
        with:
          version: '6.11.2'
          arch: win64_msvc2022_64
      - name: 一键验证（4 种组合：Qt6/Qt5 x 静态/动态）
        shell: pwsh
        run: pwsh -NoProfile -File scripts/validate.ps1 -QtBin "$env:QT_ROOT_DIR/bin"
      - name: AddressSanitizer（库 + 测试 + 示例）
        shell: pwsh
        run: pwsh -NoProfile -File scripts/validate.ps1 -Asan -Library Static -QtBin "$env:QT_ROOT_DIR/bin"
      - name: Release（构建 + CTest + 示例 + 基准 + 安装消费端）
        shell: pwsh
        run: pwsh -NoProfile -File scripts/validate.ps1 -Release -Library Static -QtBin "$env:QT_ROOT_DIR/bin"
      - name: MinGW（GCC + Clang，Qt 安装器自带的 kit）
        shell: pwsh
        # 需要 runner 上装有 Qt 的 mingw / llvm-mingw kit 与对应工具链；路径用 -MinGWKits 覆盖。
        run: pwsh -NoProfile -File scripts/validate.ps1 -MinGW -Library Static

  ubuntu-qt6:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y qt6-base-dev qt6-base-dev-tools
      - run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
      - run: cmake --build build
      # 无显示器：测试与示例都用 offscreen 平台插件（CTest 已经在测试属性里设好了）
      - run: ctest --test-dir build --output-on-failure --no-tests=error
      - run: sudo apt-get install -y xvfb
      - name: 示例自检（必须退出 0，且不允许任何 qWarning）
        # 这里用 Xvfb + xcb（而不是 offscreen）：offscreen 插件自己会发
        # "This plugin does not support propagateSizeHints()"，配上 QT_FATAL_WARNINGS=1
        # 会把每个示例都变成失败。装好字体（fontconfig + dejavu）后 xcb 下没有这类噪声。
        # 本机（Windows/MSVC）没有这条路径，改由 scripts/validate.ps1 扫描示例输出里的
        # 库类名来达到同样目的，见 §3。
        run: |
          for exe in build/bin/*; do
            case "$exe" in
              *tst_*|*bench_*) continue ;;
            esac
            xvfb-run -a env QT_FATAL_WARNINGS=1 "$exe" --exit-after 800
          done

  ubuntu-asan:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y qt6-base-dev qt6-base-dev-tools
      - run: >
          cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
          -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
          -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
      - run: cmake --build build
      - run: ctest --test-dir build --output-on-failure --no-tests=error
        env:
          # Qt 自己会在退出时留下进程级分配，泄漏检测按失败处理会把每个用例都变成红：
          # 这一组盯的是 use-after-free / 越界 / 未定义行为。
          ASAN_OPTIONS: detect_leaks=0
```

## 3. 环境坑（本地验证时踩到的，写进 workflow 之前先看一眼）

* **测试与示例都需要一个平台插件**：CI 上没有显示器，必须
  `QT_QPA_PLATFORM=offscreen`。CTest 里已经通过测试属性设好；示例要用 `env:` 给。
* **`QT_FATAL_WARNINGS=1` 不能无脑全开**，两边都会被误伤：
  - **单元测试**里有若干**故意**发 `qWarning()` 的路径（span 重叠、pin 超过上限、表头方向不匹配、
    pane 列表被规范化、行号条超过镜像上限……），对应的用例正是靠这些警告断言行为的，变成致命错误
    会让它们直接中止；
  - **offscreen 平台插件**自己会发 `This plugin does not support propagateSizeHints()`，
    而 Qt 6 起不再随包提供字体、缺字体时 `QFontDatabase` 又会发 `Cannot find font directory ...`
    —— 本机实测：12 个示例在 `QT_QPA_PLATFORM=offscreen` + `QT_FATAL_WARNINGS=1` 下**全部**
    以退出码 3（abort）结束，而它们在自己退出码 0 的正常运行里不打印任何东西。
  所以：Linux job 用 Xvfb + xcb（上一条 YAML）来获得这个门禁；本机脚本改用"扫描示例输出里的
  库类名"（库的诊断一定会带上 `VirtualTableView::…` 这类类名，见 §2 的
  `scripts/validate.ps1` 第 3 步），实测 12 个示例干净、而带库警告的测试输出会被判红。
* **ASan 要关掉泄漏检测**：`detect_leaks=0`，否则 Qt 的进程级残留会让全绿变全红；
  UBSan 保留默认行为（`-fno-sanitize-recover` 可选，便于把 UB 直接变成失败）。
* **Windows 上的 sanitizer 只有 ASan（MSVC `/fsanitize=address`），没有 UBSan**：`-Asan`
  走的就是这条路（见 §5）。UBSan 与 GCC/Clang 那组仍然只能在 Linux runner 上做。
* **Windows 侧不要自己拼 vcvars**：`scripts/validate.ps1` 已经处理 Qt 路径、vcvars 与
  构建树选择，直接给它 `-QtBin`（可给多个 kit）；它跑的是这些 kit × 静态/动态的所有组合，
  runner 上没有 Qt 5 时用 `-QtBin <单个 kit>` + `-Library Both` 即可，也可以用
  `-SkipExamples` / `-SkipBenchmarks` / `-SkipConsumer` 裁剪步骤。

## 4. 接进来之后要改的文档

* [abi.md](abi.md) §5 的支持矩阵：把"未实测"的行改成实测。
* [roadmap.md](roadmap.md) §3e 与 Wave 4 行：`CI 未接入` → 已接入，并把 job 名写进去。
* README 顶部徽章区：加 CI 徽章（只有 CI 真的绿过一次之后才加）。

## 5. 本机已经跑过的 sanitizer（MSVC ASan，2026-09-26）

`scripts/validate.ps1 -Asan` 是本机可复跑的那一半 sanitizer验证（Windows + MSVC 19.50 +
Qt 6.11.2 / 5.15.2，静态、Debug）：

```powershell
pwsh -File scripts/validate.ps1 -Asan -Library Static -QtBin D:\devlib\Qt\6.11.2\msvc2022_64\bin
pwsh -File scripts/validate.ps1 -Asan -Library Static -QtBin D:\devlib\Qt\5.15.2\msvc2019_64\bin
```

它对每个 kit 建一个独立构建树（`cmake-build-debug-qt{6,5}-asan`），配置项是
`-DCMAKE_CXX_FLAGS=/fsanitize=address`，运行期 `ASAN_OPTIONS=detect_leaks=0`；基准与"安装 +
消费端"在这一模式下跳过（前者在 ASan 下要跑几分钟，后者是独立 CMake 工程、得自己带上
sanitizer 选项）。

实测结果（两个 kit 都是）：`build all`、`ctest`（28 个目标：单元 / 变异 / GUI 交互全覆盖）、
`examples`（12 个，`--exit-after` 退出码 0）全绿，**没有任何 ASan 报告**（UAF、越界、double free
都没有出现）。这覆盖了审查"ASan/UBSan"里能在 Windows 上做的部分；剩下的 UBSan 与 GCC/Clang
组合仍需要一个 Linux runner（配置见 §2）。同一轮里脚本新增了示例输出扫描（§3 的
`QT_FATAL_WARNINGS` 替代方案）：12 个示例在正常退出码 0 的同时不打印任何库诊断。

同一轮还把 **Release** 做成了可复跑的一半（审查的 "Release benchmark smoke"）：

```powershell
pwsh -File scripts/validate.ps1 -Release -Library Static
```

它用独立构建树 `cmake-build-release-qt{6,5}` 跑 configure → `all` → 28 个 CTest 目标 →
12 个示例 → 三档基准 → 安装 + 消费端。实测两个 Qt 版本共 14 步全绿；数字（Debug/Release 对照）
记在 [performance.md](performance.md) §3 的 Release 表。

### GCC / Clang（Windows 上的 MinGW kit，2026-09-26）

同一个脚本还有 `-MinGW`：三个 kit 各自建树（`cmake-build-{debug,release}-mingw-<kit>`），
不导入 vcvars，而是把 kit 自己的编译器 bin 放到 PATH 前面、并把
`-DCMAKE_CXX_COMPILER=<kit>/g++|clang++` 传给主工程与安装消费端：

```powershell
pwsh -File scripts/validate.ps1 -MinGW -Library Static            # Debug
pwsh -File scripts/validate.ps1 -MinGW -Library Static -Release   # Release
```

| kit | Qt | 编译器 | 结果 |
| --- | --- | --- | --- |
| `gcc13-qt6` | 6.11.2 / `mingw_64` | GCC 13.1.0 | 静态 Debug 与 Release、动态 Debug 各 7 步全绿（构建 / 28 CTest / 12 示例 / 3 基准 / 安装 / 消费端），`-Wall -Wextra -Wpedantic` 零警告 |
| `clang17-qt6` | 6.11.2 / `llvm-mingw_64` | Clang 17.0.6 | 同上，零警告 |
| `gcc81-qt5` | 5.15.2 / `mingw81_64` | GCC 8.1.0 | 同上（本机最老的组合；**动态库**这一格查出"公开常量没导出"） |

这一轮 GCC/Clang 报出并修掉的问题都是 MSVC 看不见的：MSVC 专有的 `long long(x)` 函数式转换
（30 处，改成 `static_cast`）、MinGW 8.1 头文件里只有 `GetProcessMemoryInfo`（`K32…` 别名需要
`_WIN32_WINNT`，所以基准改为 `GetProcessMemoryInfo` + `psapi`）、3 处缺 `override`、1 处未用常量、
1 处 Qt 6 弃用的 `QMouseEvent` 构造。**Linux 仍然未实测**（那需要一台 Linux 机器或 runner：
平台相关的字体查找、D-Bus、X11/Wayland 坐标都不在 Windows MinGW 的覆盖范围内）。

动态库（`-Library Shared`）在 GCC/Clang 下另有一个只有它才会暴露的问题：`static constexpr`
成员是 C++17 的隐式 `inline` 变量，库自己把它当常量表达式读、DLL 里不产生定义，而消费者那侧是
`dllimport` —— 一旦消费者把常量绑到引用（`QCOMPARE` 的参数就是 `const T&`）就会
`undefined reference to __imp_...`。现在库在各自 `src/**` 里保留七个公开常量的地址强制导出，
并有 `tst_virtualitemview::publicConstantsAreAddressableFromAConsumer` 守住。

### UBSan（llvm-mingw Clang，2026-09-26）

MSVC 既没有 UBSan 也没法混编，但 llvm-mingw 的 Clang 可以：

```powershell
pwsh -File scripts/validate.ps1 -UBSan -Library Static      # = clang17-qt6 + UBSan
```

它在 `cmake-build-debug-mingw-clang17-qt6-ubsan` 里用
`-fsanitize=undefined -fno-sanitize-recover=undefined`（任何一处 UB 直接 abort，而不是只打印
一行警告）跑 configure → `all` → 28 个 CTest 目标 → 12 个示例 → 3 档基准，实测 5 步全绿。
插桩确实生效：`llvm-nm libVirtualItemViews.a` 里能看到 `__ubsan_handle_*` 引用。安装消费端在
这一模式下跳过（独立 CMake 工程，得自己带上同样的 flag）。
