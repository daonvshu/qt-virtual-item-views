# VirtualWrapView、WrapLayout 与 WrapWidget

项目按顺序排列，每项保留自己的宽度，超过可用宽度时换行，所有项使用统一高度。

| 使用场景 | 类型 | 数据与控件 | 高度和滚动 |
| --- | --- | --- | --- |
| 大量项目，需要模型、选择和控件复用 | `VirtualWrapView` | model + `WidgetAdapter` | 可启用自适应固定高度，超过独立上限后垂直滚动 |
| 少量控件，放进现有 QWidget | `WrapLayout` | 直接 `addWidget()` | 提供 `heightForWidth()`，高度由父布局分配；布局本身不滚动 |
| 少量控件，需要自动缩高和上限 | `WrapWidget` | 内置公开的 `WrapLayout` | 默认按内容固定高度；超过独立的自适应高度上限后内部滚动 |

三种用法共用换行计算，支持独立项目间距、行间距、内容边距、行对齐及 RTL。默认统一高度为 32，项目和行间距均为 8；不自动加入外侧半间距，内容边距由调用者明确配置。行尾空白不创建项目，也不参与选择和 hover。

## 虚拟视图接入

```cpp
auto *view = new viv::VirtualWrapView(parent);
view->setAdapter(new TagAdapter, true);
view->setFixedItemHeight(32);
view->setItemWidthRange(40, 240);
view->setItemSpacing(8);
view->setRowSpacing(8);
view->setContentMargins(QMargins(8, 8, 8, 8));
view->setModel(model);
view->setAutoHeightEnabled(true);
view->setAutoHeightMaximumHeight(240);
```

宽度依次取 `setItemWidth(index, width)`、模型 `Qt::SizeHintRole` 的 `QSize::width()`、adapter 的 `estimatedSize(type, index).width()`；没有有效值时使用 100。先应用全局宽度范围，再限制到当前行的可用宽度。过宽项目独占一行，文本省略由业务控件处理。`SizeHintRole` 的高度不影响统一高度。

`clearItemWidth()` / `clearItemWidths()` 恢复自然宽度，`setItemWidths()` 批量设置显式宽度。显式宽度随持久索引移动，模型重置后失效。`dataChanged` 会重新估算受影响项目；若 adapter 的字体或其他外部估算条件改变，调用 `invalidateItemWidths()`。

不会创建所有控件来测量 `sizeHint()`。首次布局需要读取所有项的宽度，视口宽度改变时需要重新排列缓存的宽度，成本为 O(N)；滚动复用已有几何，不重新估算全部项目。局部宽度变更从受影响的显示行附近重新排列后缀，多次几何失效由事件循环合并。

模型每一行对应一个项目，`SelectRows` 也只选择该项目，不选择同一个显示行的其他项目。`displayRowForIndex()` 返回当前布局行号，窗口变宽后它可能改变。持久业务身份仍应使用 model index 或业务 ID。

左右键在相邻项目间移动，上下键寻找相邻显示行中水平位置最近的项目。拖放支持 Automatic / Manual 起拖、项目间插入、末行空白追加、模型权限检查和边缘自动滚动；这一版采用插入指示器，不提供二维项目让位动画。

状态背景由框架绘制，adapter 控件保持透明即可。`setStateBackgroundCornerRadius()`、`setHoverBackgroundColor()`、`setSelectedBackgroundColor()` 和原有状态动画配置均可使用。主体颜色、item 颜色、圆角边框、QSS、性能日志也沿用通用视图接口。

## 直接添加真实控件

```cpp
auto *wrap = new viv::WrapWidget(parent);
wrap->setAutoHeightMaximumHeight(180);
wrap->wrapLayout()->setFixedItemHeight(32);
wrap->wrapLayout()->setContentsMargins(8, 8, 8, 8);
wrap->addWidget(new QPushButton(tr("Pending")));
parentLayout->addWidget(wrap);
```

`WrapWidget` 默认启用自适应高度。内容、控件宽度、字体或布局参数变化后，框架合并更新并使用 `setFixedHeight()` 将控件高度设为完整换行高度，加上边框、视口边距和占位的强制水平滚动条。父布局不能把它压缩到更小的高度；父容器空间不足时，由业务安排外层滚动或设置合适的上限。

`setAutoHeightMaximumHeight(height)` 设置控件总高度上限，`autoHeightMaximumHeight()` 查询；默认 `0` 表示不限，负值也按 `0` 处理。不设上限时始终按内容高度计算，设置后超出部分使用内部滚动。该上限独立于 QWidget 的尺寸约束，不会被内部 `setFixedHeight()` 覆盖。autoheight 开启期间不要用 QWidget 的 `setMinimumHeight()`、`setMaximumHeight()` 或 `setFixedHeight()` 管理高度。

调用 `setAutoHeightEnabled(false)` 会释放内部固定高度，恢复接管高度前的 QWidget 最小、最大高度及原垂直尺寸策略，回到普通 `QScrollArea` 的布局行为；再次开启时重新按内容计算。自适应高度上限会保留。`VirtualWrapView` 也使用相同的固定高度和独立上限规则。

`VirtualWrapView` 内容没有竖向滚动范围时，竖向滚轮事件交给外层处理；`WrapWidget` 内容无需滚动时也会放行滚轮，便于放入外层滚动区域。是否可以内部滚动按实际内容范围判断，不依赖滚动条是否可见或是否达到最大高度。

真实控件宽度取自身 `sizeHint()` 并考虑最小、最大尺寸。隐藏控件默认不占位置；设置 `retainSizeWhenHidden` 后保留位置。控件文本、size hint 或可见性变化通过布局失效更新，不需要手动重建。显式最小尺寸仍是 QWidget 的约束，超出可用空间的部分由容器裁剪；应避免给标签设置超过容器的最小宽度。

`addWidget()` / `insertWidget()` 将 QWidget 所有权交给容器；`takeWidget(index)` 隐藏并解除父对象后返回，调用者负责销毁或重新加入；`clear()` 销毁全部容器内控件。通过 `wrapLayout()->takeAt()` 直接移除时遵循 QLayout 规则，调用者负责布局项，控件 QObject 所有权不自动变化。内置 content widget 和 layout 不能替换。

只需要普通布局时，直接 `new viv::WrapLayout(container)` 后 `addWidget()`。布局没有滚动条，也不替父容器决定最大高度。

## 按功能查找示例

所有章节由一个 [wrap_capabilities](../examples/wrap_capabilities/README.md) 程序演示，代码和 `.ui` 各自独立。以下路径均相对于仓库根目录。

| 功能 | 源码相对路径 |
| --- | --- |
| 基础接入 | [examples/wrap_capabilities/01_basic/example.cpp](../examples/wrap_capabilities/01_basic/example.cpp) |
| 宽度优先级、间距、对齐和 RTL | [examples/wrap_capabilities/02_geometry/example.cpp](../examples/wrap_capabilities/02_geometry/example.cpp) |
| 状态、圆角、选择和颜色 | [examples/wrap_capabilities/03_visual_states/example.cpp](../examples/wrap_capabilities/03_visual_states/example.cpp) |
| 结构体、多控件类型和事件取索引 | [examples/wrap_capabilities/04_binding_events/example.cpp](../examples/wrap_capabilities/04_binding_events/example.cpp) |
| 代理模型、排序、过滤和显示根 | [examples/wrap_capabilities/05_model_root/example.cpp](../examples/wrap_capabilities/05_model_root/example.cpp) |
| 滚动定位、保留控件和性能诊断 | [examples/wrap_capabilities/06_scrolling/example.cpp](../examples/wrap_capabilities/06_scrolling/example.cpp) |
| 自动和手动拖放 | [examples/wrap_capabilities/07_drag_drop/example.cpp](../examples/wrap_capabilities/07_drag_drop/example.cpp) |
| 虚拟视图自适应高度 | [examples/wrap_capabilities/08_auto_height/example.cpp](../examples/wrap_capabilities/08_auto_height/example.cpp) |
| 公开 WrapLayout | [examples/wrap_capabilities/09_wrap_layout/example.cpp](../examples/wrap_capabilities/09_wrap_layout/example.cpp) |
| WrapWidget 自适应高度及所有权 | [examples/wrap_capabilities/10_wrap_widget/example.cpp](../examples/wrap_capabilities/10_wrap_widget/example.cpp) |

不等高项目、表头、冻结行列、合并格和树展开不属于 Wrap 的功能范围；这些场景使用对应的表格、树或瀑布流视图。
