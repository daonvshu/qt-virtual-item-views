# Wrap 能力示例

一个 `wrap_capabilities` 进程，十个独立章节。每章包含自己的 `.ui`、model、adapter 和控件实现，可单独复制，不依赖其他章节。

| 章节 | 内容 | 源码 |
| --- | --- | --- |
| 01 | 基础接入 | [01_basic/example.cpp](01_basic/example.cpp) |
| 02 | 宽度、间距与对齐 | [02_geometry/example.cpp](02_geometry/example.cpp) |
| 03 | 状态、颜色与圆角 | [03_visual_states/example.cpp](03_visual_states/example.cpp) |
| 04 | 结构体、多类型与事件 | [04_binding_events/example.cpp](04_binding_events/example.cpp) |
| 05 | 模型、代理与显示根 | [05_model_root/example.cpp](05_model_root/example.cpp) |
| 06 | 滚动、物化与诊断 | [06_scrolling/example.cpp](06_scrolling/example.cpp) |
| 07 | 自动与手动拖放 | [07_drag_drop/example.cpp](07_drag_drop/example.cpp) |
| 08 | 虚拟视图自适应高度 | [08_auto_height/example.cpp](08_auto_height/example.cpp) |
| 09 | 直接使用 WrapLayout | [09_wrap_layout/example.cpp](09_wrap_layout/example.cpp) |
| 10 | 真实控件自适应高度 | [10_wrap_widget/example.cpp](10_wrap_widget/example.cpp) |

在已有 CMake 构建中启用 `VIRTUALITEMVIEWS_BUILD_EXAMPLES`，构建 `wrap_capabilities` 目标。运行 `wrap_capabilities --example 8` 可直接进入自适应高度章节。项目支持 Qt 5.12.3+ 与 Qt 6.2+；运行时使用对应 Qt 的运行库。

新项目只需链接 `VirtualItemViews::VirtualItemViews` 与 Qt Widgets，并对示例目标启用 `AUTOUIC`。每个章节的 `createExample()` 返回一个可放入自己窗口的 QWidget。

函数用途与生命周期说明位于英文代码注释中。API 选择及宽度计算说明见 [docs/wrap.md](../../docs/wrap.md)。
