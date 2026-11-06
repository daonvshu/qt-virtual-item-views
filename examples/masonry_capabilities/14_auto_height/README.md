# 自适应高度

少量项目时高度跟随所有列的最大底部；超过最大高度或父布局可用高度才滚动。不需要内部滚动时，滚轮交给外层滚动区域。

入口为 [example.cpp](example.cpp)，页面布局为 [auto_height.ui](auto_height.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
