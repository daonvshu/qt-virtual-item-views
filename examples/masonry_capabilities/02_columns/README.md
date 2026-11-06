# 列数与列宽

列数 0 表示自动计算；正数请求固定列数，仍受最小列宽限制。列不是数据字段，改变窗口宽度会重新排布。

入口为 [example.cpp](example.cpp)，页面布局为 [columns.ui](columns.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
