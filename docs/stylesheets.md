# 默认对象名与样式表

`VirtualListView`、`VirtualTreeView`、`VirtualTableView` 和 `VirtualTreeTableView` 的内置控件在构造时设置默认 `objectName`，业务无需额外命名。名称表示组件用途，同一名称可以对应多个 section 或冻结表头，并非唯一实例标识。名称不会在绑定、滚动或复用时重新设置，业务可自行修改。

| 组件 | 默认 objectName |
| --- | --- |
| 视图本身（所有派生视图共用） | `vivItemView` |
| 内容 viewport | `vivViewport` |
| 水平 / 垂直滚动条 | `vivHorizontalScrollBar` / `vivVerticalScrollBar` |
| 水平 / 垂直表头，包括冻结副本 | `vivHorizontalHeader` / `vivVerticalHeader` |
| 内置表头 section，包括复用实例 | `vivHeaderSection` |
| 默认左上角控件 | `vivHeaderCorner` |
| 行 / 列间距容器 | `vivRowSpacingHost` / `vivColumnSpacingHost` |
| 列宿主 `ColumnHost` | `vivColumnHost` |
| 树表 branch 绘制层 | `vivBranchOverlay` |
| 拖放指示器 | `vivDropIndicator` |

把样式表设置到需要配置的 view，即可限制在该视图及其子控件范围内。例如无需调用任何 `setObjectName()`：

```cpp
view->setStyleSheet(QStringLiteral(
    "#vivItemView { background-color: #f5f7fb; }"
    "#vivHorizontalHeader { background-color: #dceaf8; }"
    "#vivVerticalHeader { background-color: #e4f1e6; }"
    "#vivHorizontalHeader #vivHeaderSection { color: #7654ad; font-weight: bold; }"
    "#vivVerticalHeader #vivHeaderSection { color: #16704a; }"
    "QScrollBar#vivVerticalScrollBar:vertical { background: #eadcf5; width: 14px; }"
    "QScrollBar#vivVerticalScrollBar::handle:vertical { background: #7654ad; min-height: 24px; }"));
```

表头规则同时命中普通和冻结 section。建议用空格组成后代选择器，不依赖 `>` 直接父子关系：物化、冻结和动画裁剪可能引入中间容器。若业务为 view 设置了其他名称，需相应修改 `#vivItemView` 规则；只使用内部名称的规则仍可匹配。

业务 adapter 创建的行、cell、间距内容和自定义 corner 保留业务自己的名称。继承 `HeaderSection` 或 `ColumnHost` 会取得对应基类默认名称，也可在自定义构造函数中替换。业务通过 Qt 接口替换 viewport 或滚动条后，其名称由业务自行决定。

## 命中选择器与绘制范围

默认名称提供选择器目标，不改变各组件原来的绘制职责：

- 先绘制整体 View 底色，再绘制 viewport QSS 背景，之后叠加项和状态背景。第一层未指定颜色时读取 View 的 `QPalette::Base` 画刷；`setBackgroundColor()` 只覆盖第一层。第二层读取匹配 `#vivViewport` 的背景画刷，未设置时不额外填色，透明时露出第一层，半透明时叠色。`setBackgroundVisible(false)` 同时关闭两层，表头和显式间距颜色仍独立。
- 表头容器的 QSS `background-color` 或渐变 `background` 覆盖整条表头，内置 section 不再绘制原生面板盖住它；默认间距共享同一底色。也可以用 `#vivHorizontalHeader #vivHeaderSection` 等选择器单独设置 section 背景。只改文字时仍保留原生面板。
- 背景优先级为：`setBackgroundVisible(false)` 关闭背景；对 section 而言，显式 `header->setSectionBackgroundColor()` 优先，其次是 `header->setBackgroundColor()`，再依次回退到 section 自己的 QSS 背景、表头容器 QSS、原生面板。`setSectionBackgroundColor(QColor())` 清除独立覆盖，不修改区域底色；传透明色露出容器，半透明色叠加在容器上。该接口不改变间距、空白或 corner，冻结 section 自动同步。
- 默认 corner 可用 `#vivHeaderCorner` 单独设置背景，未设置时跟随水平表头；自定义 corner 自行管理绘制。纯色、透明、半透明和渐变通过 Qt 调色板画刷绘制，不解析 QSS 文本，也不依赖 Qt 私有接口。背景图片、QSS border 和 QSS border-radius 不在这条绘制路径的支持范围内，边框与圆角使用框架接口。
- section 的文字可使用 QSS 的颜色和字体；显式 header 文字颜色及模型的 `ForegroundRole` 可以覆盖默认文字颜色。
- hover、selected、项底色、普通分割线及冻结分割线使用各自接口控制，状态动画继续叠加在 QSS 底色上。显式表头间距颜色优先于容器背景；仅给 section 设置颜色不会改变间距。给绘制宿主设置对象名，不会让其自绘图形自动支持所有 QSS 属性。
- `QHeaderView::section` 仍不匹配这些控件，因为本框架的表头类型是 `VirtualHeaderView`。`vivHeaderSection` 是真实 QWidget 的名称。

内部绘制与裁剪组件也有名称，便于定位：`vivItemPaneClipHost`、`vivPaneClipHost`、`vivCellPaneContent`、`vivTransientItemClipHost`、`vivItemPaneSeparatorLine`、`vivPaneSeparatorLine`、`vivPaneGapGridLines`、`vivHeaderDragSeparator`、`vivListRowGridLine`、`vivTreeRowGridLine`、`vivRowGridLine`、`vivTreeDragPlaceholderOutline`。兼容样式控件使用 `vivStyledTableRow` / `vivGridCoverOverlay`；流式和瀑布流网格层使用 `vivFlowGridOverlay` / `vivMasonryGridOverlay`。这些组件按需创建，不保证始终存在，不应依赖它们的数量或层级组织业务逻辑。

## 透明托管层

编辑器宿主 `vivEditorHost`、行/列裁剪容器 `vivItemPaneClipHost` / `vivPaneClipHost`、cell 滚动内容层 `vivCellPaneContent`、树动画裁剪层 `vivTransientItemClipHost`、`ColumnHost` 和 `WrapWidget` 的 `vivWrapContent` 只负责托管、定位或裁剪。它们默认保持透明，不显示来自外层通配 QSS 的背景、背景图片或边框，也不引入 QSS padding/margin。

透明规则只命中这些托管层，不匹配其中的业务子控件。编辑器、按钮、标签等仍可通过各自的 stylesheet 设置外观。需要设置整行或 cell 底色时，使用 `setItemBackgroundColor()` 或配置业务控件；需要设置表头、间距和分割线时，使用对应组件接口。

## 可运行示例

例如同时设置整体底色和内容区域底色：

```cpp
view->setStyleSheet(QStringLiteral(
    "#vivItemView { background-color: #c4edda; }"
    "#vivItemView #vivViewport { background-color: rgba(255, 80, 160, 100); }"));
```

viewport 颜色不改变表头或滚动条自身的背景；透明表头能露出下方的 View 底色。背景渐变按各层完整矩形计算，局部重绘不会改变渐变范围；用于清除旧状态的间距／行控件底色也使用相同坐标。`backgroundBrush()` 仍只返回 View 底层画刷，不包含 viewport 上层；它不能用一支画刷表达两层半透明混合。

框架在 View 启用 QSS 时自动让 viewport 参与独立样式匹配，因此规则直接写在 View 或其祖先的 stylesheet 中即可，无需再调用 `viewport()->setStyleSheet()`。业务已为 viewport 设置本地 stylesheet 时，框架保留其内容。

背景默认读取 `QPalette::Base`。Qt 对透明或半透明的 viewport QSS 背景可能将 `Base` 清为 `NoBrush`；框架仅在已匹配 QSS 背景且出现该情况时读取 `Window` 中保留的画刷，保留透明度和渐变。无需业务修改调色板，也不会解析 stylesheet 字符串。

- List：[examples/list_capabilities/22_colors/example.cpp](../examples/list_capabilities/22_colors/example.cpp)
- Tree：[examples/tree_capabilities/28_colors/example.cpp](../examples/tree_capabilities/28_colors/example.cpp)
- Table：[examples/table_capabilities/32_colors/example.cpp](../examples/table_capabilities/32_colors/example.cpp)
- TreeTable：[examples/treetable_capabilities/37_colors/example.cpp](../examples/treetable_capabilities/37_colors/example.cpp)

四个页面均有可编辑 QSS 预设；两个 table 页面额外演示 section / corner 背景、整条表头纯色 / 渐变 / 透明，以及仅修改文字的后代选择器。可勾选冻结列与间距观察匹配范围，清除 QSS 后恢复原生外观。
