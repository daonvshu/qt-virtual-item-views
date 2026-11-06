# 滚动、物化与诊断

大量数据不必预先创建 QStandardItem 和 QWidget。overscan 按每列项目计数；pin 保留指定物化项。性能探测还需打开 viv.performance.* 日志分类。

入口为 [example.cpp](example.cpp)，页面布局为 [scrolling.ui](scrolling.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
