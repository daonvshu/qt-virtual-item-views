# 示例导航

`VirtualListView` 按功能查找源码见 [docs/list.md](../docs/list.md)，路径均相对于仓库根目录。基础接入直接阅读 [examples/list_capabilities/01_basic/example.cpp](list_capabilities/01_basic/example.cpp)。一个 `list_capabilities` 进程演示各项功能，每个目录包含自己的 model、adapter 和行控件；构建、启动与复制方式见 [examples/list_capabilities/README.md](list_capabilities/README.md)。

`VirtualTableView` 使用同样的独立目录结构，按功能查找见 [docs/table.md](../docs/table.md)，构建与启动见 [table_capabilities/README.md](table_capabilities/README.md)。

`VirtualTreeView` 按功能查找见 [docs/tree.md](../docs/tree.md)，每个能力目录独立实现，由 [tree_capabilities](tree_capabilities/README.md) 统一演示。

`VirtualTreeTableView` 按功能查找见 [docs/tree-table.md](../docs/tree-table.md)，统一程序为 [treetable_capabilities](treetable_capabilities/README.md)。

`VirtualFlowTableView` 按功能查找见 [docs/flow-table.md](../docs/flow-table.md)，由 [flow_capabilities](flow_capabilities/README.md) 统一演示，每章自带模型、adapter、控件和布局文件。

`VirtualMasonryView` 按功能查找见 [docs/masonry.md](../docs/masonry.md)，由 [masonry_capabilities](masonry_capabilities/README.md) 演示独立卡片高度、最短列布局、图片和自适应高度。

`VirtualWrapView`、`WrapLayout` 和 `WrapWidget` 使用 [wrap_capabilities](wrap_capabilities/README.md) 演示不等宽换行、虚拟控件复用和少量真实控件。按功能查找见 [docs/wrap.md](../docs/wrap.md)。

## 按视图入门

| 视图 | 首先阅读 | Adapter / 控件 | 后续参考 |
| --- | --- | --- | --- |
| `VirtualListView` | [独立能力示例](list_capabilities/README.md)、[基础列表](list_capabilities/01_basic/example.cpp) | 各能力目录自行定义 | [行高](list_capabilities/06_item_heights/README.md)、[状态背景](list_capabilities/08_visual_states/README.md)、[行内事件](list_capabilities/11_item_events/README.md)、[可选平滑滚动](list_capabilities/19_smooth_scrolling/README.md) |
| `VirtualTableView` 行物化 | [行控件](table_capabilities/01_row_widgets/example.cpp) | 各能力目录自行定义 | [整行结构体](table_capabilities/04_binding_context/example.cpp)、[行表头换序](table_capabilities/22_row_reordering/example.cpp) |
| `VirtualTableView` cell 物化 | [单元格控件](table_capabilities/02_cell_widgets/example.cpp) | 各能力目录自行定义 | [圆角行 hover](table_capabilities/10_visual_states/example.cpp)、[冻结与间距](table_capabilities/16_frozen_areas/example.cpp) |
| `VirtualTreeView` | [独立能力示例](tree_capabilities/README.md)、[基础树](tree_capabilities/01_basic/example.cpp) | 各能力目录自行定义 | [展开动画](tree_capabilities/07_expansion/example.cpp)、[branch 图标](tree_capabilities/10_branch_icons/example.cpp)、[节点结构体](tree_capabilities/04_binding_context/example.cpp) |
| `VirtualTreeTableView` | [独立能力示例](treetable_capabilities/README.md)、[基础多列树](treetable_capabilities/01_basic/example.cpp) | 各能力目录自行定义 | [行控件](treetable_capabilities/02_row_widgets/example.cpp)、[cell 控件](treetable_capabilities/03_cell_widgets/example.cpp)、[节点结构体](treetable_capabilities/05_binding_context/example.cpp) |
| `VirtualFlowTableView` | [基础接入](flow_capabilities/01_basic/example.cpp) | 各能力目录自行定义 | [尺寸与换列](flow_capabilities/02_cell_sizes/example.cpp)、[分割线](flow_capabilities/03_spacing_grid/example.cpp)、[图片卡片](flow_capabilities/09_images/example.cpp) |
| `VirtualMasonryView` | [基础接入](masonry_capabilities/01_basic/example.cpp) | 各能力目录自行定义 | [高度更新](masonry_capabilities/05_height_updates/example.cpp)、[图片卡片](masonry_capabilities/12_images/example.cpp)、[自适应高度](masonry_capabilities/14_auto_height/example.cpp) |

## 按交互查找

### 惯性滚动与刷新分页

分别运行 `list_capabilities --example 24`、`table_capabilities --example 34`、`tree_capabilities --example 30`、`treetable_capabilities --example 39`、`flow_capabilities --example 12`、`wrap_capabilities --example 11` 或 `masonry_capabilities --example 15`。链接 SmoothScrollbar 时，勾选“滚轮惯性”可用滚轮体验平滑滚动。顶部下拉可刷新，触底时追加下一页。占位模式下页脚展开至 28 个逻辑像素，覆盖模式使用透明背景。[分页和滚轮控制器接入代码](refresh_demo/page.cpp)

**由视图发起分页。** 绑定模型并连接 `loadMoreRequested` 后启用分页。让异步回调随页面销毁而取消，并在 GUI 线程处理结果。

```cpp
connect(view, &viv::VirtualItemView::loadMoreRequested, this, [this](quint64 id) {
    fetchNextPage(this, [this, id](const PageResult &page) {
        if (!view->isRequestCurrent(id)) return;
        if (page.failed()) { view->failRequest(id); return; }
        model->appendItems(page.items);
        view->finishLoadMore(id, page.hasMore);
    });
});
view->setLoadMoreEnabled(true);
```

**由页面发起新查询。** 绑定模型后，在发起搜索或切换筛选条件时领取新请求号。写入模型前校验请求号，避免较慢的旧响应替换当前结果。完成首屏后，视图根据 `hasMore` 和视口空余空间决定是否请求下一页。

```cpp
const auto id = view->beginExternalPage();
if (!id) return;
fetchFirstPage(query, this, [this, id](const PageResult &page) {
    if (!view->isRequestCurrent(id)) return;
    if (page.failed()) { view->failRequest(id); return; }
    model->replaceItems(page.items);
    view->finishExternalPage(id, page.hasMore);
});
```

`loadMorePauseReasonChanged` 可驱动重试按钮。`ExternalPageFailed` 时重新发起当前查询；分页失败（`RequestFailed`）或仍有下一页却没有新增项（`EmptyPage`）时调用 `retryLoadMore()`。外部首屏和显式 `setLoadMoreSuspended(true)` 各自生效，显式暂停由调用方解除。

| 需求 | 最小参考 |
| --- | --- |
| hover / selected 时显示操作控件；事件处理拿当前正确 index | [状态关联内容](list_capabilities/09_state_content/README.md)、[行内事件](list_capabilities/11_item_events/README.md) |
| 表格 cell 物化时显示整行 hover 和圆角 | [状态背景](table_capabilities/10_visual_states/example.cpp) |
| 每行只存一份结构体 | [整行结构体绑定](table_capabilities/04_binding_context/example.cpp) |
| 不同层级节点使用不同控件类型和数据类型 | [节点结构体与深度](tree_capabilities/04_binding_context/example.cpp) |
| 树表每个 cell 直接拿整行结构体 | [节点结构体与绑定上下文](treetable_capabilities/05_binding_context/example.cpp) |
| 改变数据后更新行高 | [独立行高示例](list_capabilities/06_item_heights/README.md) |
| 拖行表头排序 | [行表头换序](table_capabilities/22_row_reordering/example.cpp) |
| 原生整行拖放、Ctrl 复制 | [整行拖放](table_capabilities/24_drag_indicator/example.cpp) |
| cell 行间插入和空白格放下 | [单格投放](table_capabilities/26_cell_drop/example.cpp) |
| 仅从手柄启动拖动、自定义预览 | [列表手柄起拖](list_capabilities/15_drag_drop/example.cpp)、[预览参数](../docs/interaction.md#手动触发与自定义预览) |

各 capabilities 目录的 README 说明构建、复制方式和模型责任。`list_capabilities`、`table_capabilities`、`tree_capabilities`、`treetable_capabilities`、`flow_capabilities`、`wrap_capabilities`、`masonry_capabilities` 分别构建对应示例，`viv_examples` 构建全部示例。
