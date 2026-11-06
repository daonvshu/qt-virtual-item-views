# 模型、代理与显示根

瀑布流只显示当前 root 的直接子项。通过代理过滤/排序后，传给视图和事件处理的索引都必须来自同一个代理模型。

入口为 [example.cpp](example.cpp)，页面布局为 [model_root.ui](model_root.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
