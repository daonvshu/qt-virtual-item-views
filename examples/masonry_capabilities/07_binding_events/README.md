# 结构体、多类型与事件

完整结构体保存在默认数据 role；bind 直接读取 context.rowData。事件只连接一次，在触发时解析控件当前索引；hover/selected 时显示操作控件。

入口为 [example.cpp](example.cpp)，页面布局为 [binding_events.ui](binding_events.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
