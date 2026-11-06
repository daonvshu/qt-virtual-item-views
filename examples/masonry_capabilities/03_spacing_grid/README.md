# 间距与分割线

分别控制行间距、列间距、内容边距和分割线。中心模式总是穿过交叉间距；两侧模式可控制是否穿过。

中心模式在内容四周预留半间距，独立于内容边距；切换两侧模式后移除半间距。间距颜色同时覆盖内部间距和外侧半间距。各列最后一项下方补齐分割线，贴近底边框时遵循 `setTrailingRowGridLineAutoHideEnabled()` 的设置。

入口为 [example.cpp](example.cpp)，页面布局为 [spacing_grid.ui](spacing_grid.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
