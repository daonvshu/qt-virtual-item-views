# 基础接入

模型的一行对应一张卡片；按最短列放置，高度由 adapter 的估算比例决定。调整窗口宽度观察自动换列。

入口为 [example.cpp](example.cpp)，页面布局为 [basic.ui](basic.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
