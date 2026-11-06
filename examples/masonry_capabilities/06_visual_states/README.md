# 状态背景与内容

背景由框架绘制，卡片保持透明；业务只根据 visualStateChanged 更新内容。快速跨项移动可观察 hover 的淡入淡出。

入口为 [example.cpp](example.cpp)，页面布局为 [visual_states.ui](visual_states.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
