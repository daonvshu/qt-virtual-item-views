# 22 颜色与样式表

运行 `list_capabilities --example 22`。本章的 `.ui`、model 和 adapter 均独立，可直接参考 [example.cpp](example.cpp)，布局见 [colors.ui](colors.ui)。函数调用旁的注释使用英文。

颜色页分别配置主体、项底色、hover、selected、内容间距、普通分割线和外框。主体默认使用 `QPalette::Base`；`setItemBackgroundColor()` 默认无效，不绘制，设置后作用于实际项目行，独立于主体显隐和状态开关。表头、空白区域和行列间距不使用项底色。半透明颜色可以观察叠色；选择默认即可恢复各接口的默认值。




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


建议依次设置暖黄色项背景、半透明 hover 和选中背景，然后切换业务 QLabel 的 QSS 底色，观察遮挡关系。再清除 QSS、将主体设为透明，确认实际项目仍有底色，而剩余空白透出灰蓝父背景。Qt 样式会影响原生控件的最终外观，应结合本机运行效果观察。

绘制顺序为整体 View 底色 → viewport QSS 背景 → 项底色 → hover / selected → 业务控件与分割线。函数 `setBackgroundColor()` 设置第一层，不覆盖独立的 viewport 层；后者透明时露出第一层，半透明时叠色。取消主体底色同时关闭前两层；项、显式间距与表头设置保持独立。两个新增的叠层预设可直接编辑并与冻结、间距、圆角一起观察。

冻结分隔线独立于普通分割线，通过冻结开关与对应颜色下拉框观察。
