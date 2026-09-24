# Widget 生命周期与 pin 规则

## 1. 状态机

```
                createWidget(type, viewport)
                        |
   (pool 空) ----------- v
                   [ created / hidden ]
                        |  bindWidget(widget, index)
                        v
                   [ bound + shown ] <------.
                        |                    | 仍在可见/overscan/pin 集合内
                        | 离开集合            | （只做 setGeometry）
                        v                    |
        unbindWidget(widget, index) ---------'
                        |
                        v
                   [ hidden / pooled ]
                        |  acquire()（可能在本轮 pass 内被新进入窗口的项复用）
                        v
                   bindWidget(widget, newIndex)
```

规则：

* 只有 `WidgetRecycler` 创建/销毁 item 控件；`View` 只 acquire/recycle。
* `acquire()` 返回的控件一定是 hidden 且已 unbind（池内不保存身份）。
* 新创建或复用的控件在 `bindWidget()` 之后才 `show()`，从不展示旧内容。
* 池有限额（默认 256/type，`WidgetRecycler::setMaxPoolSize()` 可改）。超限回收走
  `deleteLater()`，`resizeEvent()` 会调用 `trim()`，避免历史大视口把池撑大。

## 2. 适配器契约（WidgetAdapter）

| 方法 | 调用时机 | 必须做什么 |
| --- | --- | --- |
| `widgetType(index)` | 每次实例化 / 池查询 | 把 index 分类到池；简单场景返回 0 |
| `createWidget(type, parent)` | 池空时 | 创建控件，parent 是 viewport |
| `bindWidget(widget, index)` | 复用或新创建后、显示前 | 用 index 数据填充控件；必须能重复调用 |
| `unbindWidget(widget, index)` | 回池/删除前、控件被重新绑定前 | 停止 QTimer/动画/QMovie/异步请求/业务订阅，清空可视内容 |
| `estimatedSize(index)` | 新插入行、Variable 模式初值 | 给出未测量前的估计尺寸 |

`bindWidget()` 可能在同一控件上被反复调用（每次滚动都可能换 index），因此实现里不能假设"一个控件
只服务一个业务对象"。需要长期引用的业务对象请使用 `QPersistentModelIndex` 或稳定业务 ID。

## 3. pin（禁止回收）

焦点、IME、popup 与显式 pin 的完整规则见 [focus-ime.md](focus-ime.md)。要点：

* 焦点在控件内（含 IME composition）或在控件拥有的 popup 上时，控件不可回收。
* 业务可以 `setItemPinned(index, true)` / `pinWidget(widget)` 显式固定某个 item。
* pin 的项在窗口外会被移动到视口外坐标（负数，被 viewport 裁剪），不遮挡内容。
* `rowsAboutToBeRemoved` 例外地强制回收并清除显式 pin——数据已经不存在。
* `stats()` 与 `setMaxPinnedItems()` 用于监控"是否 pin 得太多"。
* `setLifecycleLoggingEnabled(true)` 打开有界生命周期日志（create/reuse/bind/unbind/recycle/pin），
  配合 `stats()` 定位回收异常。

## 4. 典型时序：滚动一行

1. `scrollContentsBy()` 通过 `ScrollMapper::toLogicalOffset(value)` 得到新的 `m_scrollOffset`。
2. `relayout()`：新进入窗口的 1 行需要控件；离开窗口的 1 行被 `unbind -> hide -> recycle`。
3. 回池的控件在本轮 pass 内被新行复用（回收先于创建），因此 `createdCount()` 不变。
4. `syncScrollBars()` 用同一锚点写回 value，滚动条位置不跳。

`benchmarks/bench_listview.cpp` 会直接打印 `widgets created while scrolling`，期望为 0。

树的滚动与 List 完全相同；差别只在结构变更时：

* `rowsAboutToBeRemoved` 会按身份回收"被删子树"里的所有可见行控件（子树的每一行都算），
  因此删除一棵展开的子树不会留下孤儿控件（`tst_virtualtreeview::rowsRemovedRecyclesTheSubtree`）。
* 展开/折叠只改变可见行集合：新进入窗口的行 acquire/bind，离开窗口的行照常回池。
* 树的缩进由视图负责（`geometryForViewRow()` 内缩行矩形），业务行控件按整行自己的坐标布局即可，
  不要自己再加缩进，否则展开/折叠或修改 `setIndentation()` 时会双重缩进。

## 5. 所有权：谁创建，谁负责

* 视图**不持有**业务控件所有权；它只持有可见 + overscan + pin 的控件，其余交给 Recycler 池，
  业务控件的 parent 始终是 viewport。
* 视图本身遵循 Qt 父子规则：`setCentralWidget(view)` / `layout->addWidget(view)` 之后，父对象会在
  析构时 `delete` 视图。因此要么 `auto *view = new VirtualListView(&window);`（Qt 持有），
  要么在栈上时**先声明父窗口、再声明视图**（视图先析构并自行从父对象注销）。
* 反例（会导致程序退出时命中 MSVC 调试堆断言 `is_block_type_valid`，在调试器中表现为
  `Exception 0x80000003` 停在 `return app.exec();`）：

```cpp
viv::VirtualListView view;   // ✗ 栈对象
QMainWindow window;          //    窗口后析构 -> delete 一个栈对象
window.setCentralWidget(&view);
```
