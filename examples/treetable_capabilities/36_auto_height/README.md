# 36 自适应高度与滚动上限

修改根与子节点数量并展开收起；高度包含表头、展开行、间距和必要滚动条，超过自适应高度上限后才内部滚动。0 表示不限高，父布局空间不足由业务处理。Designer 页面显示实际和建议高度及滚动范围。

入口：[example.cpp](example.cpp)。主要接口：`setAutoHeightEnabled`、`setAutoHeightMaximumHeight`、`setTrailingRowGridLineAutoHideEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 36`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。布局见 [auto_height.ui](auto_height.ui)，需要启用 CMake `AUTOUIC`。

## 嵌入外层滚动容器

`auto_height.ui` 将视图放在 `QScrollArea` 的内容布局中，并启用 `widgetResizable`。下方留有外层内容和底部标记，可以直接观察滚轮实际滚动的是哪一层；无需添加事件过滤器或手动转发滚轮。

- 保持自适应高度开启，减少数据使内部竖向滚动范围为 0，在项目、空白区域上滚动，应移动外层内容。带表头的视图也可把鼠标放在水平、垂直表头上观察。
- 增加数据，使内容超过最大高度，再把鼠标放在视图上滚动，应滚动内部项目。树和树表还可通过展开、收起改变内容高度。
- “父区域上限”仅限制 `previewHost`，不会压缩视图；空间不足需要业务处理。自适应高度上限设为 0 时完整展示内容，外层视口不作为上限。

外层预留内容用于演示嵌套滚动，业务中替换为自己的其他控件即可。本章节保持独立，不依赖其他示例代码。

外层内容容器不设置固定最小高度；额外的 500px 留白由布局 spacer 提供，用来演示其他表单内容。视图自身按内容固定高度，`setAutoHeightMaximumHeight()` 限制其总高度，0 表示不限；不依赖祖先容器类型或可用空间。
