# 选择与几何查询

选择保存的是模型索引，与物化控件是否还在屏幕内无关。ExtendedSelection 使用 Ctrl 增减、Shift 扩选；键盘按空间邻近项目导航。

入口为 [example.cpp](example.cpp)，页面布局为 [selection.ui](selection.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
