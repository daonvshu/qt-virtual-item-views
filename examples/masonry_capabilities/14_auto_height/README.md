# 自适应高度

少量项目时高度跟随所有列的最大底部；超过自适应高度上限才滚动，父布局空间不足由业务处理。不需要内部滚动时，滚轮交给外层滚动区域。

入口为 [example.cpp](example.cpp)，页面布局为 [auto_height.ui](auto_height.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。

外层内容容器不设置固定最小高度；额外的 500px 留白由布局 spacer 提供，用来演示其他表单内容。视图自身按内容固定高度，`setAutoHeightMaximumHeight()` 限制其总高度，0 表示不限；不依赖祖先容器类型或可用空间。
