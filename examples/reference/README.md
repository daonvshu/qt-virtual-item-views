# 最小参考程序

`VirtualListView` 功能示例源码路径见 [docs/list.md](../../docs/list.md)，基础接入位于 [examples/list_capabilities/01_basic/example.cpp](../list_capabilities/01_basic/example.cpp)。表格按功能查找见 [docs/table.md](../../docs/table.md)，统一程序为 [table_capabilities](../table_capabilities/README.md)。本目录提供树、树表、流式表格和瀑布流的独立参考程序。

每个 `.cpp` 都是一个独立程序入口，只展示一个使用主题。先读入口里的模型、adapter、视图装配顺序，再按 `#include` 阅读对应的控件或模型。没有示例注册器、隐藏初始化流程或可选图片 / 滚动组件。

## 构建和复制

使用项目现有 CMake 配置，保持 `VIRTUALITEMVIEWS_BUILD_EXAMPLES=ON`。例如，在已经配置好的构建目录中只编译一个程序：

```sh
cmake --build <BUILD_DIR> --target ref_tree_basic --config Debug
```

编译本目录全部程序使用目标 `viv_reference_examples`。输出目录沿用项目的 `bin`，多配置生成器还会包含配置子目录。Qt 的运行库和平台插件需要按通常方式部署；这些程序只链接 VirtualItemViews 和 Qt Widgets。

复制到自己的工程时，复制入口文件及其直接包含的本目录头文件，保留相对目录或调整 include。自己的目标链接：

```cmake
target_link_libraries(your_example PRIVATE VirtualItemViews::VirtualItemViews)
```

`main()` 中显式写出所有功能设置。`list/textadapter.h`、`table/textadapters.h` 只创建 / 绑定 / 清理文本控件；`tree/samplemodel.h` 只生成数据。复制功能代码时可用自己的 adapter 和模型替换它们。

## 视图和功能索引

| 目标 | 入口 | 核心写法 / 需要一起阅读的文件 |
| --- | --- | --- |
| `ref_tree_basic` | [tree/basic.cpp](tree/basic.cpp) | 层级模型、展开、动画、框架状态背景；[样例树数据](tree/samplemodel.h) |
| `ref_tree_branch` | [tree/branch.cpp](tree/branch.cpp) | branch 格宽、图标尺寸 / 颜色 / 替换、叶子对齐 |
| `ref_tree_binding_context` | [tree/binding_context.cpp](tree/binding_context.cpp) | 节点类型、不同结构体、存储的 `WidgetType`、`context.depth` |
| `ref_tree_table_rows` | [tree_table/row_widgets.cpp](tree_table/row_widgets.cpp) | 层级数据配合行控件；与普通表格行模式对照 |
| `ref_tree_table_cells` | [tree_table/cell_widgets.cpp](tree_table/cell_widgets.cpp) | 子节点 cell 绑定、冻结第一列；与普通表格 cell 模式对照 |
| `ref_tree_table_binding_context` | [tree_table/binding_context.cpp](tree_table/binding_context.cpp) | 每个节点一份结构体，cell 取得 `depth` / `logicalColumn` / `rowData` |
| `ref_flow_basic` | [flow/basic.cpp](flow/basic.cpp) | 一列模型、自适应显示列数、卡片宽高和间距 |
| `ref_masonry_basic` | [masonry/basic.cpp](masonry/basic.cpp) | `AdapterEstimate`、每张卡片的独立估算高度 |

表格合并格、自定义表头、多 pane 与拖放见 [表格功能索引](../table_capabilities/README.md)。跨视图拖放见 [drag_drop](../drag_drop/README.md)。

## 生命周期和模型责任

- 模型和外部 adapter 应早于视图声明，保证视图析构时它们仍然存在。需要引用视图的 adapter，在示例中通过 `takeOwnership = true` 交给视图管理。
- `createWidget()` 只连接一次事件；绑定时完整刷新数据。事件发生时查询 `indexForWidget()`，不要捕获第一次绑定的 index。也可在每次绑定时替换控件保存的 `QPersistentModelIndex`，解绑时清空。
- `unbindWidget()` / `unbindCellWidget()` 清理旧绑定相关活动。文本示例只需清空内容，业务中的定时器、异步任务和订阅也应停止。
- `ItemBindingContext` 传递整行 / 节点数据，不代替模型结构和 Qt 通知。表格仍需报告正确列数，结构体放在每行 / 节点的第零列。
- 拖放模式只决定命中和指示器。权限、MIME、数据修改和撤销由 model / 业务实现。[表格第 26 章](../table_capabilities/26_cell_drop/example.cpp) 演示本模型内单格投放，复杂的多格相对位置见综合 `drag_drop`。
- `setMoveRemovesSourceRows(true)` 用于 Qt 标准复制插入后由视图删除源行；模型已自己移动时保持 false。单元格移动应清空源格，不能删除整行。
