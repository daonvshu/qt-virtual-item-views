# 圆角外边框

框架采用整体 Alpha 合成。`VirtualItemView` 提供公共接口，列表、表格和树视图均可继承使用，无需额外包装容器。

```cpp
view->setFrameCornerRadius(20);
view->setFrameBorderWidth(2);
view->setFrameBorderColor(QColor("#4079ad"));
```

半径和宽度使用逻辑像素，默认均为零，不安装合成效果。负数归零，边框宽度上限为 32767；合成时将外轮廓对齐到源图的物理像素边界，边框宽度乘设备像素比后统一四舍五入，四边使用相同的物理像素厚度。绘制半径按当前尺寸限制到短边的一半。无效边框颜色使用调色板的 `QPalette::Mid`。对应读取接口为 `frameCornerRadius()`、`frameBorderWidth()` 和 `frameBorderColor()`。

边框在视图内部预留空间，表头、视口和滚动条随之布局。内轮廓半径为 `max(0, 外框半径 - 边框宽度)`；hover／selected 圆角仍通过 `setVisualStateCornerRadius()` 独立配置。将外框半径和宽度都设为零会移除框架的合成效果并恢复启用前的 QFrame 样式。

实现位于 `src/core/virtualitemviewframe.cpp`：`QGraphicsEffect` 获取整个视图及其子控件的画面，以抗锯齿 Alpha 遮罩裁出内轮廓，再合成外框。透明角可直接透出父控件的渐变、棋盘格或图片背景；没有固定颜色覆盖层。遮罩按图像大小、设备像素比和轮廓缓存，参数变化会使缓存失效。

启用该能力会占用视图的 graphics effect 槽，并替换已有的 effect；启用期间应保留框架安装的 effect 和 QFrame 布局设置。每次更新仍需获取源图并合成结果，大视图、高 DPI 和连续动画的开销取决于实际场景。视觉圆角不使用 `QWidget::setMask()`，鼠标命中范围仍为矩形。

## 少量数据

表格视口默认使用 `QPalette::Base`，不再按列宽把背景条延长到末行以外。默认行号背景和列间距的竖线仅覆盖实际行及其间距；冻结上下行分别裁剪，空白中心不绘制延长的列线。整行 hover／selected 在左右冻结列之间保持连续，显式列间距仍遵循原有状态贯通配置。自定义表头背景色保留整条容器填充语义。

末行底边保留。普通行分隔线与冻结行分隔线均按实际列区域裁剪，不横穿冻结列之间的空白区域；水平滚动、列宽和冻结配置变化时同步更新裁剪。冻结边线包含真实列间距，底部冻结行的上边线位于行顶边外侧，避免遮挡状态背景的首行像素；中间空白的命中与拖放解析会排除属于冻结区域的行。

## 主体背景

`VirtualListView`、`VirtualTreeView`、`VirtualTableView` 和 `VirtualTreeTableView` 继承同一组主体背景接口：

```cpp
view->setBackgroundVisible(false);               // Expose the parent background.
view->setBackgroundVisible(true);                // Restore the configured body base.
view->setBackgroundColor(QColor("#f5f7fa"));      // Configure a body color.
view->setBackgroundColor(QColor(Qt::transparent)); // Or use a transparent body color.
view->setBackgroundColor(QColor());              // Follow QPalette::Base again.
```

默认 `backgroundVisible()` 为 `true`，`backgroundColor()` 为无效颜色，使用当前调色板的 `QPalette::Base`。显隐优先于颜色，关闭再开启后保留已配置颜色。`backgroundBrush()` 返回解析后的画刷，隐藏时返回 `Qt::NoBrush`，可供业务绘制复用。颜色与显隐修改立即刷新主体、内置间距和样式控件，无需重新绑定数据。

表头基础背景默认开启，section、冻结表头与默认 corner 使用原生面板样式；通过 `horizontalHeader()`、`verticalHeader()` 的 `setBackgroundColor()` 单独配置颜色，或调用 `setBackgroundVisible(false)` 使对应表头透明。hover／selected、分割线及外框不受主体背景开关影响。显式间距颜色、业务 adapter 控件、自定义表头或 corner 自行绘制的底色仍由各自配置控制；滚动条保留 Qt 样式。

## 示例

运行 `table_capabilities --example 30`。页面直接调用框架接口，支持纯色、渐变和棋盘格背景，以及 200×12、3×3、1×1 和 0×3 数据。可组合表头、行列间距、四边冻结和滚动条策略，检查边缘、空白区及行状态。

[第 30 节操作说明](../examples/table_capabilities/30_borders/README.md)

列表与树也直接使用同一组公共接口，各章拥有独立的 model、adapter 和 `.ui` 页面：

- 列表：`list_capabilities --example 20`，见 [第 20 章源码](../examples/list_capabilities/20_borders/example.cpp) 和 [操作说明](../examples/list_capabilities/20_borders/README.md)，可组合行间距、首末冻结行及少量数据。
- 树：`tree_capabilities --example 26`，见 [第 26 章源码](../examples/tree_capabilities/26_borders/example.cpp) 和 [操作说明](../examples/tree_capabilities/26_borders/README.md)，可组合分支区域显隐、展开收起及空模型。

两页均可调整外框半径、边框宽度和颜色，以及独立的行状态圆角；可切换纯色、渐变和棋盘格父背景查看透明角，不依赖 SmoothScrollbar。
