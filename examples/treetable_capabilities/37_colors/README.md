# 37 颜色与样式表

运行 `treetable_capabilities --example 37`。本章的 `.ui`、model 和 adapter 均独立，可直接参考 [example.cpp](example.cpp)，布局见 [colors.ui](colors.ui)。函数调用旁的注释使用英文。

颜色页分别配置主体、项底色、hover、selected、内容间距、普通分割线和外框。主体默认使用 `QPalette::Base`；`setItemBackgroundColor()` 默认无效，不绘制，设置后作用于实际项目行，独立于主体显隐和状态开关。表头、空白区域和行列间距不使用项底色。半透明颜色可以观察叠色；选择默认即可恢复各接口的默认值。

树节点的 branch 图标单独使用 setBranchIconColor()；其下方与缩进区域属于项背景。
横纵表头可独立设置底色，默认使用原生 section 样式。另有表头文字、间距和状态开关；默认 corner 跟随水平表头，勾选自定义 corner 可查看独立 QLabel 的底色。冻结首末列用于检查冻结区域。

页面“水平／垂直表头区域底色”对应 `setBackgroundColor()`，“水平／垂直 section 底色”对应 `setSectionBackgroundColor()`，具体调用在 `Example::applyColors()`。Table 和 TreeTable 均可使用：

```cpp
view->horizontalHeader()->setBackgroundColor(QColor("#dceaf8"));
view->verticalHeader()->setBackgroundColor(QColor("#e4f1e6"));

// Override only the sections; empty space and the corner keep the container base.
view->horizontalHeader()->setSectionBackgroundColor(QColor("#fff0c2"));
view->verticalHeader()->setSectionBackgroundColor(QColor("#b9dfff"));

// Optional: keep header gaps a different color.
view->horizontalHeader()->setSpacingBackgroundColor(QColor("#ffffff"));

// Clear the section override and follow the container again.
view->horizontalHeader()->setSectionBackgroundColor(QColor());

// Restore QSS backgrounds, or native panels when no QSS background is set.
view->horizontalHeader()->setBackgroundColor(QColor());
view->horizontalHeader()->setSpacingBackgroundColor(QColor());
```

section 底色统一作用于该方向的所有内置 section，冻结副本同步，不改变间距、表头空白和默认 corner；默认 corner 继续跟随水平表头区域底色。此接口没有按 logicalIndex 设置单个 section 的参数。选“默认 / 未设置”清除 section 覆盖；选“透明”跳过原生面板并露出容器底色；半透明色叠加在容器上。

背景关闭开关同时控制区域和 section 底色。开启时，显式 section 色优先于区域色，再回退到原有 QSS／原生路径；hover / selected 动画保持独立。可把区域设为浅蓝、section 设为暖黄，并打开间距、冻结首末列，观察不同区域及冻结副本的颜色。

Stylesheet 页提供预设、可编辑源码和应用 / 清除按钮。QSS 仅设置到当前预览视图，避免影响示例导航。下方状态栏显示 View 和 viewport 的 Base 值与主体画刷类型，方便观察实际 Qt 样式行为。

框架组件直接使用默认对象名，本页只给业务控件命名。完整名称表和绘制边界见 [默认对象名与样式表](../../../docs/stylesheets.md)。默认名称只在构造时设置，业务可以替换；行控件的名称由 adapter 决定。

| 预设 | 观察方法 |
| --- | --- |
| `#vivItemView` 纯色 / 渐变 | 主体颜色保持默认时读取 View 调色板画刷；显式主体颜色优先。不是完整的 QSS 背景图片、定位与圆角支持 |
| `#vivItemView #vivViewport` | 内容区域背景叠加在整体 View 底色之上；表头和滚动条使用各自背景。关闭主体底色会同时关闭 View 和 viewport 两层 |
| View 与 Viewport：半透明叠加 / 内容渐变 | 设置不同的上下层颜色，检查透明混合、完整 viewport 渐变、间距和滚动中的一致性 |
| `QLabel#colorItem` | 业务控件自己绘制背景，会遮住下面的项和状态层。清除 QSS 可恢复透明 |
| `QWidget` | 广泛匹配子控件，容易使业务控件不透明。显式框架绘制层仍按各自接口配置 |
| `#vivItemView QScrollBar#vivVerticalScrollBar` | 使用框架默认对象名；勾选大量数据，观察独立滚动条样式 |
| `QHeaderView::section` | 不匹配本框架的 VirtualHeaderView，使用 header 便捷接口设置默认 section |

建议依次设置暖黄色项背景、半透明 hover 和选中背景，然后切换业务 QLabel 的 QSS 底色，观察遮挡关系。再清除 QSS、将主体设为透明，确认实际项目仍有底色，而剩余空白透出灰蓝父背景。Qt 样式会影响原生控件的最终外观，应结合本机运行效果观察。

绘制顺序为整体 View 底色 → viewport QSS 背景 → 项底色 → hover / selected → 业务控件与分割线。函数 `setBackgroundColor()` 设置第一层，不覆盖独立的 viewport 层；后者透明时露出第一层，半透明时叠色。取消主体底色同时关闭前两层；项、显式间距与表头设置保持独立。两个新增的叠层预设可直接编辑并与冻结、间距、圆角一起观察。

冻结分隔线独立于普通分割线，通过冻结开关与对应颜色下拉框观察。

“整条表头”预设使用框架默认的 `vivHorizontalHeader` / `vivVerticalHeader`，同时匹配冻结表头，支持纯色、渐变和透明背景，默认 section 与间距跟随容器底色。“section 与 corner 背景”通过后代选择器分别设置列头、行号和左上角；显式 header 背景颜色优先于 QSS。默认 corner 名为 `vivHeaderCorner`，未单独设置时跟随水平表头，自定义 corner 保留业务自己的名称。

“表头 section 文字”只修改文字，保留原生背景；显式 header 文字颜色或模型的 ForegroundRole 优先。切换背景预设时可打开间距与冻结列、关闭状态背景比较各层；清除 QSS 后恢复原生面板。背景样式支持 Qt 调色板表达的颜色和渐变，边框、圆角及动画继续用框架接口控制。
