# 自动与手动拖放

自动模式从卡片主体按下并移动起拖；手动模式只响应左侧手柄。模型负责 MIME 和数据插入，框架负责预览、pin、落点和结束清理。

入口为 [example.cpp](example.cpp)，页面布局为 [drag_drop.ui](drag_drop.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
