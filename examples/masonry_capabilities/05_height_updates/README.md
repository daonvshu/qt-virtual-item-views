# 高度更新与动画

估算 QSize 是比例；setItemHeight 是当前项目的权威像素高度，优先于其他高度模式。单项刷新会重排其后的最短列布局。

入口为 [example.cpp](example.cpp)，页面布局为 [height_updates.ui](height_updates.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
