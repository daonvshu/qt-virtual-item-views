# 构建流程

## 环境要求

- CMake 3.16 或更新版本。
- Qt 5.12.3+（C++14）或 Qt 6.2+（C++17），包含 Core、Gui、Widgets 模块。
- 与所选 Qt 套件兼容的 C++ 编译器。Windows 使用 MSVC 时，在对应的开发者命令行中执行；使用 MinGW 时，将对应编译器加入 PATH。
- 使用 Ninja 生成器时，需将 Ninja 加入 PATH。

以下命令从源码根目录执行。`<QT_PREFIX>` 为 Qt 套件安装前缀，`<INSTALL_PREFIX>` 为库的安装目录。

## 配置与编译

使用 Ninja 构建 Release 静态库：

```sh
cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=<QT_PREFIX>" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target VirtualItemViews
```

静态库为默认配置。需要动态库时，在配置命令中添加 `-DVIRTUALITEMVIEWS_BUILD_SHARED=ON`；需要 Debug 时，将 `CMAKE_BUILD_TYPE` 改为 `Debug`。不同 Qt 套件、编译器或构建形态使用各自的构建目录。

使用 Visual Studio 等多配置生成器时，不设置 `CMAKE_BUILD_TYPE`，在编译和安装时通过 `--config` 选择配置：

```sh
cmake -S . -B build "-DCMAKE_PREFIX_PATH=<QT_PREFIX>"
cmake --build build --target VirtualItemViews --config Release
```

已有构建目录可直接执行对应的 `cmake --build` 命令进行增量编译，无需重新配置。

交叉编译时，在首次配置命令中添加 `-DCMAKE_TOOLCHAIN_FILE=<TOOLCHAIN_FILE>`，并使用目标平台的 Qt 套件；编译器、sysroot 和目标平台查找路径由工具链文件指定。

## 构建示例

示例默认不参与构建。配置时开启示例选项，再编译所需目标：

```sh
cmake -S . -B build "-DCMAKE_PREFIX_PATH=<QT_PREFIX>" -DVIRTUALITEMVIEWS_BUILD_EXAMPLES=ON
cmake --build build --target table_capabilities --config Release
```

可选目标包括 `list_capabilities`、`table_capabilities`、`tree_capabilities`、`treetable_capabilities`、`flow_capabilities`、`wrap_capabilities` 和 `masonry_capabilities`。章节及运行方式见[示例导航](../examples/README.md)。

## 安装与链接

```sh
cmake --install build --prefix "<INSTALL_PREFIX>" --config Release
```

安装配置应与已编译配置一致。安装后，在应用工程的 `CMAKE_PREFIX_PATH` 中加入库的安装前缀和 Qt 套件前缀，并链接导出的目标：

```cmake
find_package(VirtualItemViews REQUIRED)
target_link_libraries(app PRIVATE VirtualItemViews::VirtualItemViews)
```

目标会传递所需头文件路径、Qt 依赖和 C++ 标准要求。模型与 adapter 的接入方式见[快速开始](getting-started.md)。
