# Focus / IME / Popup 与 pin 规则

Recycler 最大的企业级风险是错误回收正在交互的 QWidget（编辑器、IME 合成、popup、拖拽源）。
VirtualItemViews 用 **pin** 表达"这个 item 现在不能被回收"。

## 1. 默认策略

| 场景 | 策略 | 判定方式 |
| --- | --- | --- |
| QLineEdit / QTextEdit 获得焦点 | pin | `QApplication::focusWidget()` 是 item widget 或其子控件 |
| IME composition | 强制 pin | 合成期间焦点必定在 item widget 内，因此与上一行同一判定 |
| QComboBox / 菜单等 popup 活跃 | pin | `QApplication::activePopupWidget()` 的 owner 链包含 item widget |
| 拖拽源 / 拖拽目标 | pin | 业务显式 `view.pinWidget(widget)` |
| 普通 hover | 不 pin | hover 不改变身份，也不阻止回收 |

判定实现在 `VirtualItemView::isPinnedItem()`，materialization pass 的回收阶段会再判断一次，
因此即使焦点是在上一次 pass 之后才转移的，也不会误回收。

## 2. 显式 pin API

```cpp
view.setItemPinned(index, true);   // 按 index pin
view.setItemPinned(index, false);

view.pinWidget(widget);            // 按当前持有的 widget pin
view.unpinWidget(widget);
```

pin 的 item 即使离开 overscan 也会保持实例化；在窗口外时它会被 `setGeometry()` 到视口外的坐标
（负数，被 viewport 裁剪），因此不会遮挡可见内容，也不会丢失编辑状态。

`pinWidget()` 对非 materialized 控件会给出 `qWarning` 并忽略。

## 3. 边界与监控

* **删除优先于 pin**：`rowsAboutToBeRemoved` 会强制回收被删除行的控件并清除其显式 pin——数据已经
  不存在，继续保留只会表现死数据。
* **软上限**：`setMaxPinnedItems(n)` 只做监控，超过时 `qWarning` 一次，不会偷偷解除业务 pin。
  该选项用于排查"widget 数量只增不减"。
* **诊断快照**：`VirtualViewStats stats() const` 返回 logical / materialized / pooled / pinned 与
  create / bind / recycle 计数：

```cpp
const viv::VirtualViewStats s = view.stats();
// logical: 1,000,000  materialized: 30  pooled: 4  pinned: 1
// create: 30  bind: 1234  recycle: 1208
```

  长期运行或性能回归时最关心的是：稳态滚动中 `materializedItems` / `pooledWidgets` 是否稳定，
  以及 `createCount` 是否保持常量（见 `benchmarks/bench_listview.cpp`）。

## 4. 反例：业务侧常见错误

* 在行控件里持有 `QTimer` 却不在 `unbindWidget()` 里停掉：回收后仍触发旧业务逻辑。
* 用 `int row` 记住"我是第几行"：insert/remove/sort 之后全部错位，应使用 `QPersistentModelIndex`
  或稳定业务 ID。
* 对滚动中需要用到的控件长期 pin：虚拟化失效，应改为在业务状态变化时按需 pin/unpin。

