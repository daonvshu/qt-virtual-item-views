# ABI、构建形态与工具链矩阵（v1.0）

> 配套文档：[api-stability.md](api-stability.md) 管**源码层面**的冻结（哪些名字不能动），
> 本文管**二进制层面**的约定（怎么编、怎么发、什么算破坏）。

## 1. 版本号与 SOVERSION

| 项 | 现在（v0.9） | v1.0 收尾时 |
| --- | --- | --- |
| `project(VERSION)` | `0.9.0` | `1.0.0` |
| `SOVERSION` | `0`（= `PROJECT_VERSION_MAJOR`） | `1` |
| 共享库文件名 | Windows `VirtualItemViews.dll`；Linux `libVirtualItemViews.so.0.9.0`（`SONAME` 指向 `.so.0`） | `.so.1` / `SOVERSION 1` |
| `find_package` 版本匹配 | `SameMinorVersion`（0.x 期间没有 ABI 承诺，要 0.9 就别给 0.10） | `SameMajorVersion`（1.x 内部保证兼容） |
| 规则 | 任何 v1.0 之前的改动直接生效、不提供兼容层（没有发布过、没有下游） | 破坏 ABI 就动 `SOVERSION`，破坏源码 API 就动主版本 |

`SOVERSION` 只在共享库上有意义（`VIRTUALITEMVIEWS_BUILD_SHARED=ON`），静态库没有这回事：
静态链接把 ABI 的账推给了使用者，所以静态用户必须用**同一套**编译器/Qt/运行库重编。

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

## 4. 什么算 ABI 破坏

以下改动都要升 `SOVERSION`（v1.0 之后 = 升主版本），并写进 CHANGELOG 的 `Breaking`：

1. 删除或改名公开类/自由函数/信号（符号直接消失）；
2. 增删虚函数，或改变虚函数顺序/签名（vtable 布局变化）；
3. 改变类的数据成员布局（增删成员、改类型、改访问级别、改对齐）——包括纯内联的结构体，
   因为它们按值出现在签名里（`TablePane`、`ColumnGeometry`、`ItemPane`…）；
4. 改变枚举的**值**（`enum class` 底层类型与数值会进 ABI；追加在末尾是源码兼容但仍改变
   `Q_ENUM` 元数据，需在 CHANGELOG 标注）；
5. 改变内联函数体里"调用方会内联展开"的语义（例如某个 getter 突然开始做懒构造）；
6. 改变公开类的基类列表（`QWidget`/`QHeaderView` 之类）；
7. 改变最低 Qt 版本或构建选项组合（例如 "6.2+" 改成 "6.5+"）。

不算破坏（可以随次版本发）：新增非虚函数、新增信号、新增类、新增枚举值（追加在末尾）、
改注释与文档、改内部实现（`src/**`）、修 bug。**注意"改默认值"属于源码语义变化**，
按 [api-stability.md](api-stability.md) 第 2 条要当成破坏性变更处理。

### 跨边界的容器

公开类里有 Qt 容器成员（`QList<MaterializedItem>` 等）和 `std::function` 成员。它们不导出符号
（默认都是内联/模板代码），但有两条必须写明的约束：

* 诊断接口把容器**按引用**交给调用方，例如 `materializedItems()` 返回 `const QList<...>&`；
  调用方用自己的编译器实例化它的模板代码，因此**库与调用方必须用同一套 Qt 版本 + 同一套
  MSVC 运行库设置**（含 `_ITERATOR_DEBUG_LEVEL`，即 Debug/Release 不能混）。
* MSVC 会对被 `dllexport` 的类里的这类成员报 C4251（本仓库实测 131 条）。`global.h` 在
  **库自身编译时**关闭 4251，并写明理由；消费端包含这些头文件不受影响（导入方向不触发）。
  这不是"把警告藏起来"：它对每个公开类都成立的前提正是上面这条"同一套工具链"约束，
  真要做跨编译器 ABI 只能上 pimpl，那是 1.x 的议题。

## 5. 支持矩阵

声明支持的最低版本由 CMake 强制（`find_package(Qt6 6.2)` / `find_package(Qt5 5.15)`），
**实测**组合如下：

| Qt | 编译器 | 静态 | 动态 | 验证方式 |
| --- | --- | --- | --- | --- |
| 6.11.2 / msvc2022_64 | MSVC 19.50 x64（VS 18 Community） | 通过 | 通过 | `all` 构建 + 20 个 CTest 目标 + 12 个示例退出码 0 |
| 5.15.2 / msvc2019_64 | 同上 | 通过 | 通过 | 同上 |

构建环境：Ninja + CMake 4.x，C++17，Debug。两个 Qt 版本各跑静态与动态各一遍，共四种组合。

**未实测**（只在 CMake 层面被接受，没有任何 CI/本机证据，使用前请自测）：

* GCC / Clang / MinGW，Linux / macOS 上任意 Qt 版本（代码里 Qt 5 兼容点见 README 兼容约定表）；
* Qt 6.2–6.10 的任意中间版本；
* 多配置生成器（VS solution、Xcode）与 `MSVC_RUNTIME_LIBRARY`（`/MT` 之类）的组合；
* 与 Qt 的 `QT_DISABLE_DEPRECATED_*`、`QT_NO_*` 裁剪宏的组合。

## 6. 发布前检查清单（v1.0）

1. `project(VERSION 1.0.0)`，`SOVERSION` 随主版本变成 1，`find_package` 兼容性切到
   `SameMajorVersion`；
2. 四种组合（Qt 5/6 × 静态/动态）全部 `all` 构建 + CTest + 示例退出码 0；
3. `cmake --install` + 最小消费端 `find_package(VirtualItemViews)` 编译并运行（roadmap 3c）；
4. 性能基线数字固化进 [performance.md](performance.md)（roadmap 3d）；
5. CHANGELOG 的 `Breaking` 段与 `docs/api-stability.md` §6 的欠账都清空。
