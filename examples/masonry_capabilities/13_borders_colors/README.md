# 圆角、边框与颜色

分别设置主体、项目、间距、状态和外边框；QSS 可命中 vivItemView / vivViewport。viewport 背景画在主体之上，项目底色仍在上层。

入口为 [example.cpp](example.cpp)，页面布局为 [borders_colors.ui](borders_colors.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
