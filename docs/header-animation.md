# 表头动画（§23/§24）

方案文档 §23/§24 定的是"两层几何 + 分类同步"的规则。这份文档写实现契约、哪些情况动画、
哪些情况必须立刻跟上，以及渲染器支持矩阵。

## 1. 两层几何

```text
HeaderGeometry（committed）
   │  列宽 / 顺序 / 隐藏 / 排序指示 / viewport offset
   ├──► 表格 body、滚动条、columnGeometry()、命中测试、无障碍、拖放
   └──► 表头渲染器的 visual geometry（动画帧位置）
```

* **committed geometry** 是唯一事实来源（§45.10）：任何 query、任何 body 绘制都只读它。
* **visual geometry** 只存在于渲染器内部：`VirtualHeaderView` 在动画期间把 section 控件
  摆在两者之间的位置。它不写回 `HeaderGeometry`，也不通知 body。

于是"动画帧"对 body 而言根本不存在：body 在 commit 那一次重新布局，之后不再动。

## 2. 哪些情况动画、哪些必须立刻

| 交互 | committed 是否变化 | 表头 | body |
| --- | --- | --- | --- |
| Hover / sort icon / badge | 否 | 业务控件自己的动画 | 不动 |
| **Section move（换序）** | 是，且立刻最终 | **视觉过渡**（默认 160 ms） | commit 时重排一次，不跟帧 |
| Resize（拖列宽） | 是，逐帧 | 立刻 | 逐帧 |
| Hide / show | 是 | 立刻 | 立刻 |
| 横向滚动 / pane 变化 | 否 | 立刻（否则会与 body 撕裂） | 逐帧 |

判据只有一条：**body 会不会跟着每一帧变**。会，表头就必须逐帧跟上（resize、滚动、pane 变化）；
不会（换序是唯一一种"先定终局、再让人看到过程"的交互），才把过程交给渲染器。

实现上靠"视觉顺序是否变化"来区分：`VirtualHeaderView::relayout()` 比较本次与上次的可见列顺序，
顺序变了且列集合没变（不是隐藏/插入/删除）才启动过渡，所以 resize / hide / 滚动都不会误触发。

## 3. API

```cpp
// 视图级（默认：启用，160 ms；0 表示关闭）
view.setHeaderAnimationEnabled(true);
view.setHeaderAnimationDuration(240);

// 渲染器级（CustomRenderer 也可以自己实现）
header->setSectionAnimationEnabled(true);
header->setSectionAnimationDuration(240);
```

设置会下发给主表头与每一个 pane 表头（§43），也会在安装新表头 / 新建 pane 表头时应用。

## 4. 渲染器支持矩阵

| 渲染器 | 支持 | 说明 |
| --- | --- | --- |
| `VirtualHeaderView`（§17–§19） | 是 | 每个可见 section 一个控件、位置由渲染器决定，所以能逐帧插值；打断时从当前位置继续（不会跳回上一帧的起点）。 |
| `NativeHeaderView`（§16） | 否（忽略） | section 的位置与绘制属于 `QHeaderView`，框架不改它的 paint；此时表头与 body 一起在 commit 时跳变。 |
| 自定义 `HeaderViewInterface` | 可选 | 实现 `setSectionAnimationEnabled()/setSectionAnimationDuration()` 即参与；不实现则是"忽略"，语义与 native 相同。 |

## 5. 打断与边界

* **动画中再次换序**：从当前视觉位置继续滑向新的 committed 位置，不回跳。
* **关闭动画**：当前正在飞行的 section 立刻落到 committed 位置（不会卡在中间）。
* **被回收的 section**：只有还在物化集合里的 section 参与过渡；新进窗口的 section 直接出现在
  自己的 committed 位置，不做"从屏幕外飞入"。
* **pinned section**（§36，交互中的控件）：与其它 section 一样按 visual geometry 摆放。
* **表头控件自己必须是 view 的子控件**：`VirtualTableView::setHorizontalHeader()` /
  `setVerticalHeader()` 会把控件 reparent 到视图。否则它是一个顶层窗口，位置按屏幕坐标解释
  （带窗口边框偏移），表头与 body 会差几个像素 —— 这是 visual geometry 依赖"自身原点"的前提。

## 6. 测试与演示

* `tests/unit/tst_virtualheaderview`：
  * `sectionMoveAnimatesWhileTheCommittedGeometryStaysAuthoritative`：换序后 committed 立刻是终值，
    section 控件仍在途中，最终落到 committed 位置。
  * `resizeAndDisabledAnimationStayImmediate`：resize 不产生过渡；关闭动画后换序立刻生效。
  * `tableForwardsTheAnimationSettings`：视图级设置下发到渲染器，端到端验证 committed/visual 分离。
* `examples/table_custom_header`：
  * 工具栏"移动一列"按钮（默认 160 ms 过渡）；
  * `--animation <ms>` 改时长；
  * `--move-demo <png>` 无人值守演示：1200 ms 慢速过渡 + 途中截图，能直接看到 section 在飞、
    body 已经在终点位置。
