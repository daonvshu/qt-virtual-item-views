# ABI、构建形态与工具链矩阵（v1.0）

> 配套文档：[api-stability.md](api-stability.md) 管**源码层面**的冻结（哪些名字不能动），
> 本文管**二进制层面**的约定（怎么编、怎么发、什么算破坏）。

## 1. 版本号与 SOVERSION

| 项 | 值（v1.0.0） |
| --- | --- |
| `project(VERSION)` | `1.0.0` |
| `SOVERSION` | `1`（= `PROJECT_VERSION_MAJOR`） |
| 共享库文件名 | Windows `VirtualItemViews.dll`；Linux/macOS `libVirtualItemViews.so.1.0.0`（`SONAME` 指向 `.so.1`） |
| `find_package` 版本匹配 | `SameMajorVersion` —— 这只是 CMake 的**包版本匹配策略**，它本身不创造 ABI 兼容性 |
| **1.x 的兼容承诺** | **源码 API / 语义兼容**（[api-stability.md](api-stability.md)）；**二进制 ABI = best effort**：换版本请重编，本项目不承诺"旧编译产物 + 新库"这种组合 |
| 规则 | 破坏源码 API 就动主版本，并写进 CHANGELOG 的 `Breaking`；`SOVERSION` 跟着主版本涨，作用只是让不同主版本的共享库文件不互相覆盖（**不给跨主版本的二进制兼容背书**）；1.0 之前没有打过 tag、没有下游，内部收口不提供兼容层，清单见 [CHANGELOG](../CHANGELOG.md) |

这条策略是 1.0 收口时**明确选定**的（2026-09-26，理由见 [roadmap.md](roadmap.md) §7 的决策记录）：
库的核心类没有 PIMPL，实现状态直接放在公开头文件里（`VirtualItemView`、`VirtualTableView`、
`VirtualHeaderView`、`NativeHeaderView`、`HeaderGeometry`、`TreeVisibilityIndex`、
`TablePaneLayout`、`WidgetRecycler`、`BlockSizeIndex`…）。如果承诺 1.x 二进制兼容，那么"给某个
缓存加一个 private 成员"就成了必须升主版本的改动 —— 而 1.0 收口前后几轮代码审查修的正是这类
内部状态。所以：**源码兼容是承诺，二进制兼容是"同一套工具链 + 重编"**。这也意味着

* 次版本里**允许**增删公开类的私有成员、改私有成员类型、给私有结构加字段（要求使用者重编）；
* 公开类里**按值出现**的类型（`TablePane`、`ColumnGeometry`、`ItemPane`、`MaterializedItem`…）
  不行 —— 它们是源码契约的一部分，改动仍然只能进主版本；
* 发布二进制包（vcpkg/Conan/自带 DLL）的人应当把版本号固定到精确版本，或在升级时重编；
  混搭不同主版本（乃至不同次版本的 Debug/Release 与不同 Qt）产生的链接错误不在支持范围。

`SOVERSION` 只在共享库上有意义（`VIRTUALITEMVIEWS_BUILD_SHARED=ON`），静态库没有这回事：
静态链接把 ABI 的账推给了使用者，所以静态用户必须用**同一套**编译器/Qt/运行库重编。

（v0.9 期间用的是 `SameMinorVersion` + `SOVERSION 0`：那时还没有 ABI 承诺，"要 0.9"的消费端
不应该被匹配到 0.10。这条规则写进 CMake 的条件分支里，1.0 之后自然走到 `SameMajorVersion`。）

## 2. 两种构建形态

```bash
# 动态库
cmake -S . -B build-shared -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DVIRTUALITEMVIEWS_BUILD_SHARED=ON -DCMAKE_PREFIX_PATH=<Qt kit>
# 静态库（默认）
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt kit>
```

符号可见性由 `include/virtualitemviews/global.h` 里的 `VIRTUALITEMVIEWS_EXPORT` 统一决定：

| 场景 | 宏 | 宏从哪来 |
| --- | --- | --- |
| 编译共享库本身 | `Q_DECL_EXPORT` | `VIRTUALITEMVIEWS_LIBRARY`，CMake 对库目标 `PRIVATE` 定义 |
| 使用共享库的应用 | `Q_DECL_IMPORT` | 不需要任何手工 define（`global.h` 的默认分支） |
| 编译或使用静态库 | 空 | `VIRTUALITEMVIEWS_STATIC`，CMake 对库目标 `PUBLIC` 定义：静态库目标自身、测试、示例、基准，以及 `find_package()` 找到这个目标的消费端都会自动带上 |

也就是说：**只有"编共享库"和"用静态库"两种情况下需要那个宏**，而且都由 CMake target
自动传播，业务代码不需要记 `#define`。

实测：`BUILD_SHARED=ON` 的 Qt 6 构建产出 `bin/VirtualItemViews.dll` + `lib/VirtualItemViews.lib`
（导入库），`dumpbin /exports` 可见约 1000 个导出符号；同一份源码在 Qt 6.11.2 与 Qt 5.15.2 下
都能共享/静态构建并跑通全部测试（见 §5）。

## 3. 构建产物布局

无论静态还是动态，可执行文件都在 `<build>/bin`，库都在 `<build>/lib`：

```bash
build/bin/table_spans        # 示例、基准、测试
build/lib/VirtualItemViews.lib / .so / .dylib
build/bin/VirtualItemViews.dll           # 共享构建时（Windows）
```

这样安排只有一个原因：Windows 只在自己所在目录（以及 PATH）里找 DLL，不会去隔壁目录找。
可执行文件与库同处 `bin/` 之后，CTest、示例、基准都不需要任何 PATH 技巧；多配置生成器
（VS/Xcode）会在下面再套一层配置名目录。

### 装出来的布局与消费方式

`cmake --install <build> --prefix <dir>` 之后（[README 安装与消费](../README.md) 有完整命令）：

```
<prefix>/include/virtualitemviews/*.h     # 24 个公开头文件（含 global.h）
<prefix>/lib/VirtualItemViews.lib         # 静态库；共享构建是导入库
<prefix>/lib/VirtualItemViews.so.<ver>    # 共享构建（Windows: <prefix>/bin/VirtualItemViews.dll）
<prefix>/lib/cmake/VirtualItemViews/
        VirtualItemViewsConfig.cmake      # find_dependency(Qt<6|5> …) + 导入 targets
        VirtualItemViewsConfigVersion.cmake
        VirtualItemViewsTargets.cmake
```

消费端只写两行：

```cmake
find_package(VirtualItemViews REQUIRED)
target_link_libraries(app PRIVATE VirtualItemViews::VirtualItemViews)
```

Qt 由 Config 里的 `find_dependency()` 找回，**但安装前缀按 Qt 大版本区分**：Config 里写死了
`find_dependency(Qt6 …)`（或 Qt5），所以 Qt 5 与 Qt 6 要各装一个前缀。静态安装会把
`VIRTUALITEMVIEWS_STATIC` 作为 `INTERFACE_COMPILE_DEFINITIONS` 带给消费端；动态安装则什么都不用带。
动态安装在 Windows 上需要 `<prefix>/bin` 里的 DLL 能被找到（exe 同目录或 `PATH`）。

`tests/install/consumer` 就是这个流程的常规检查：一个**独立**的 CMake 工程（独立 configure、
独立 build），只用 `find_package()` + 公开头文件，跑 28 项运行期自检后按退出码报告结果。

## 4. 什么算二进制破坏

下表是"二进制层面发生了什么"。因为 §1 选定的策略是"1.x 只承诺源码兼容"，所以每一行都要看
**两列**：它是不是源码破坏（决定要不要升主版本），以及它对二进制使用者的实际后果。

| # | 改动 | 源码层面 | 二进制层面（1.x 内的做法） |
| --- | --- | --- | --- |
| 1 | 删除或改名公开类 / 自由函数 / 信号 | 破坏，只能进主版本 + CHANGELOG `Breaking` | 符号消失，旧产物必然链接失败 |
| 2 | 增删虚函数，或改虚函数顺序 / 签名 | 破坏，进主版本 | vtable 布局变化；没有 PIMPL 时无法在次版本里悄悄加虚函数 |
| 3 | 改公开类**私有数据成员**的布局（增删、改类型、改对齐） | 不破坏（私有成员不是源码契约） | **允许随次版本改**：`SOVERSION` 不变，使用者升级时必须重编 —— 这正是选方案 B 换来的空间 |
| 4 | 改**按值出现**的公开结构体（`TablePane`、`ColumnGeometry`、`ItemPane`、`MaterializedItem`…）的成员 | 破坏，进主版本 | 即使只加私有成员也会改 sizeof/偏移，所以不区分公私一律进主版本 |
| 5 | 改枚举的**值** | 破坏（数值 + `Q_ENUM` 元数据），进主版本 | 追加在末尾是源码兼容但仍改元数据，要在 CHANGELOG 标注 |
| 6 | 改内联函数体里"调用方会内联展开"的语义（例如某 getter 突然开始懒构造） | 语义变化，按 [api-stability.md](api-stability.md) 第 2 条的"不改默认值语义"处理 | 允许随次版本改，要求重编 |
| 7 | 改公开类的基类列表（`QWidget`/`QHeaderView` 之类） | 破坏，进主版本 | 对象布局变化 |
| 8 | 改最低 Qt 版本或构建选项组合（例如 "6.2+" 改成 "6.5+"） | 破坏，进主版本 | 依赖的符号集合变化 |

不算破坏（可以随次版本发）：新增非虚函数、新增信号、新增类、新增枚举值（追加在末尾）、
改注释与文档、改内部实现（`src/**`）、修 bug（包括上面第 3、6 类那种"要重编"的内部调整）。
**注意"改默认值"属于源码语义变化**，按 [api-stability.md](api-stability.md) 第 2 条要当成
破坏性变更处理。

### 跨边界的容器

公开类里有 Qt 容器成员（`QList<MaterializedItem>` 等）和 `std::function` 成员。它们不导出符号
（默认都是内联/模板代码），但有两条必须写明的约束：

* 诊断接口把容器**按引用**交给调用方，例如 `materializedItems()` 返回 `const QList<...>&`；
  调用方用自己的编译器实例化它的模板代码，因此**库与调用方必须用同一套 Qt 版本 + 同一套
  MSVC 运行库设置**（含 `_ITERATOR_DEBUG_LEVEL`，即 Debug/Release 不能混）。
* MSVC 会对被 `dllexport` 的类里的这类成员报 C4251（本仓库实测 131 条）。`global.h` 在
  **库自身编译时**关闭 4251，并写明理由；消费端包含这些头文件不受影响（导入方向不触发）。
  这不是"把警告藏起来"：它对每个公开类都成立的前提正是上面这条"同一套工具链"约束，
  换句话说，"没有 PIMPL"是刻意的选择而不是欠账（[roadmap.md](roadmap.md) §7 的决策记录）。
  跨编译器 / 跨运行库的二进制复用（同一个 `.dll` 被 MSVC 2019 与 2022、或 GCC 与 Clang 的产物
  共用）**不在承诺范围内**：真要做那件事只能给这些类上 PIMPL，而那属于 2.0 的议题。

## 5. 支持矩阵

声明支持的最低版本由 CMake 强制（`find_package(Qt6 6.2)` / `find_package(Qt5 5.15)`），
**实测**组合如下：

| Qt | 编译器 | 静态 | 动态 | 验证方式 |
| --- | --- | --- | --- | --- |
| 6.11.2 / msvc2022_64 | MSVC 19.50 x64（VS 18 Community） | 通过 | 通过 | Debug 与 Release 各：`all` 构建 + 28 个 CTest 目标 + 12 个示例退出码 0 + 三档基准 + 安装消费端；另有一个 AddressSanitizer 静态构建（`scripts/validate.ps1 -Asan`）同样全绿 |
| 5.15.2 / msvc2019_64 | 同上 | 通过 | 通过 | 同上（含 Release 与 ASan） |
| 6.11.2 / mingw_64 | **GCC 13.1.0**（Qt 在线安装器的 `mingw1310_64`） | 通过 | 通过 | `scripts/validate.ps1 -MinGW`：Debug 与 Release 各 `all` 构建 + 28 个 CTest 目标 + 12 个示例 + 三档基准 + 安装消费端；`-Wall -Wextra -Wpedantic` 下**零警告** |
| 6.11.2 / llvm-mingw_64 | **Clang 17.0.6**（`llvm-mingw1706_64`） | 通过 | 通过 | 同上；默认警告级别下零警告（修掉 3 处 `override` 缺失、1 处未用常量、1 处 Qt 6 弃用构造） |
| 5.15.2 / mingw81_64 | **GCC 8.1.0**（Qt 安装器的 `mingw810_64`） | 通过 | 通过 | 同上（本机最老的组合：Qt 5.15 + GCC 8.1；动态库这一格就是这样查出"公开常量没导出"的） |

构建环境：Ninja + CMake 4.x，C++17。MSVC 的两个 Qt 版本各跑静态与动态各一遍（四种组合，
Debug + Release），MinGW / llvm-mingw 三个 kit 的静态与动态也都跑通（静态的 Debug + Release
各 21 步、动态的 Debug 各 7 步）。
MinGW 组合的复跑命令：`pwsh -File scripts/validate.ps1 -MinGW -Library Static`（路径可用
`-MinGWKits` / `-NinjaBin` 覆盖），详见 [ci.md](ci.md) §5。

**未实测**（只在 CMake 层面被接受，没有任何 CI/本机证据，使用前请自测）：

* Linux / macOS 上任意 Qt 版本（**Windows 上的 GCC 与 Clang 已实测**，见上表；平台相关的部分
  如 `QFontDatabase` 的字体查找、D-Bus 集成、X11/Wayland 的窗口内坐标仍未覆盖）；
* Qt 6.2–6.10 的任意中间版本；
* 多配置生成器（VS solution、Xcode）与 `MSVC_RUNTIME_LIBRARY`（`/MT` 之类）的组合；
* 与 Qt 的 `QT_DISABLE_DEPRECATED_*`、`QT_NO_*` 裁剪宏的组合；
* Linux 下的 ASan/UBSan（Windows 侧已经两样都跑过：MSVC ASan 与 llvm-mingw Clang UBSan，
  见 [ci.md](ci.md) §5）。

## 6. 发布前检查清单（v1.0）

0. [x] **发布策略定案**（2026-09-26）：1.x 只承诺**源码 API / 语义兼容**，二进制 ABI 是
   best effort（§1 与 §4 按这条写）；`SameMajorVersion` 只当包版本匹配策略用，不写成 ABI 承诺。
   理由与被否决的另一条路线（打 tag 前 PIMPL 化 9 个公开类）见 [roadmap.md](roadmap.md) §7；
1. [x] `project(VERSION 1.0.0)`，`SOVERSION` = 1，`find_package` 兼容性 `SameMajorVersion`；
2. [x] `pwsh -File scripts/validate.ps1` 全绿（四种组合 × 构建 / CTest / 12 个示例 / benchmark /
   安装 + 消费端共 28 个步骤，退出码 0）；
2b. [x] `pwsh -File scripts/validate.ps1 -Asan`（Qt 6.11.2 与 5.15.2 各一个 MSVC ASan 静态构建）
   全绿：28 个 CTest 目标 + 12 个示例，无 ASan 报告；UBSan / Linux 组合见 [ci.md](ci.md)；
2c. [x] `pwsh -File scripts/validate.ps1 -Release -Library Static` 全绿（两个 Qt 版本各
   构建 / 28 个 CTest / 12 个示例 / 三档基准 / 安装消费端共 14 步），Release 基线进
   [performance.md](performance.md) §3；
2d. [x] `pwsh -File scripts/validate.ps1 -MinGW -Library Static`（GCC 13.1 / Clang 17.0.6 /
   GCC 8.1）Debug 与 Release 各 21 步全绿，两个编译器在最高警告级别下零警告；
2e. [x] `pwsh -File scripts/validate.ps1 -UBSan -Library Static`（llvm-mingw Clang 的
   `-fsanitize=undefined -fno-sanitize-recover=undefined`）28 个 CTest + 12 个示例 + 3 档基准
   全绿，插桩有 `__ubsan_handle_*` 符号为证；
3. [x] 性能基线数字固化进 [performance.md](performance.md)（roadmap 3d）；
4. [x] CHANGELOG 的破坏性变更段与 [api-stability.md](api-stability.md) §6 的欠账都清空。

下一步是**打 tag**（由仓库主人手动执行）。打完之后再改动的第一件事应当是更新本文 §1 的表格与
CHANGELOG —— 1.x 的**源码兼容**承诺从这一版开始生效；二进制层面按 §1：换版本请重编，
私有成员的调整允许发生在次版本里。
