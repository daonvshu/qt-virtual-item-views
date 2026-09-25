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
| **Section move（拖动 `VirtualHeaderView` 的列）** | 只在松手时变一次 | 拖动期间是**视觉预览**：被拖的列跟随指针，邻居**平滑**让出/合上插入位，松手提交后过渡收敛 | 提交时重排一次，不跟帧 |
| **Section move（换序，`MoveAnimation::Animate`）** | 是，且立刻最终 | **视觉过渡**（默认 300 ms，OutCubic） | commit 时重排一次，不跟帧 |
| Section move（换序，程序化 / 默认 `Immediate`） | 是，且立刻最终 | 立刻 | 立刻 |
| Resize（拖列宽） | 是，逐帧 | 立刻 | 逐帧 |
| Hide / show | 是 | 立刻 | 立刻 |
| 横向滚动 / pane 变化 | 否 | 立刻（否则会与 body 撕裂） | 逐帧 |

判据只有一条：**body 会不会跟着每一帧变**。会，表头就必须逐帧跟上（resize、滚动、pane 变化）；
不会（换序是唯一一种"先定终局、再让人看到过程"的交互），才把过程交给渲染器 —— 而且**只有被明确
请求的换序才播**：程序化设置列顺序（`moveColumn()` 默认、模型换序、状态恢复）不应该变出一个没人
要求的动画。

实现上靠"视觉顺序是否变化"来区分：`VirtualHeaderView::relayout()` 比较本次与上次的可见列顺序，
顺序变了且列集合没变（不是隐藏/插入/删除）、并且收到了一次性请求（`setSectionMoveAnimated(true)`）
才启动过渡，所以 resize / hide / 滚动 / 程序化换序都不会误触发。

## 3. API

```cpp
// 视图级（默认：启用，300 ms，OutCubic；0 表示关闭）
view.setHeaderAnimationEnabled(true);
view.setHeaderAnimationDuration(240);

// 程序化换序：默认即时；需要过渡时显式请求（只影响表头，committed 几何立刻生效）
view.moveColumn(1, 4);                                   // 立刻
view.moveColumn(1, 4, VirtualTableView::MoveAnimation::Animate);

// 渲染器级（CustomRenderer 也可以自己实现）
header->setSectionAnimationEnabled(true);
header->setSectionAnimationDuration(240);
header->setSectionMoveAnimated(true);   // 一次性请求，被下一次 relayout 消费
```

设置会下发给主表头与每一个 pane 表头（§43），也会在安装新表头 / 新建 pane 表头时应用。

## 4. 渲染器支持矩阵

| 渲染器 | 支持 | 说明 |
| --- | --- | --- |
| `VirtualHeaderView`（§17–§19） | 是 | 每个可见 section 一个控件、位置由渲染器决定，所以能逐帧插值；打断时从当前位置继续（不会跳回上一帧的起点）。 |
| `NativeHeaderView`（§16） | 否（忽略） | section 的位置与绘制属于 `QHeaderView`，框架不改它的 paint；此时表头与 body 一起在 commit 时跳变。 |
| 自定义 `HeaderViewInterface` | 可选 | 实现 `setSectionAnimationEnabled()/setSectionAnimationDuration()` 即参与；不实现则是"忽略"，语义与 native 相同。 |

## 5. 打断与边界

* **拖动（§22）**：按下只记录，指针越过 `QApplication::startDragDistance()` 才算拖动（所以"点一下"和一两个像素的抖动仍然是点击/排序，不是换序——这条曾经是缺陷：旧实现一滑进邻居就提交一次，既不能连续拖，也会把点击误判成拖动）。拖动期间 `HeaderGeometry` 一个字节都不动，只改渲染器的视觉几何；松手时**一次提交**，然后由上面的过渡从预览位置收敛到 committed 位置。拖动被限制在同一个 pane 的列内（pane 表头只显示自己那几列）。按 Esc 取消，不提交。
* **让位也是动画**：插入槽位变化时，被拖的列**精确**跟随指针，其他列用**同一套缓动**（OutCubic + 默认 300 ms）滑向自己的新槽位，而不是瞬移。让位由 `QVariantAnimation` 驱动，指针停住时照样走完；过渡中途槽位再变则以"当前所在位置"为起点重新开始，不会回跳。`setSectionAnimationEnabled(false)` 或时长为 0 时让位与其它过渡一样即刻生效。
* **动画中再次换序**：从当前视觉位置继续滑向新的 committed 位置，不回跳。
* **关闭动画**：当前正在飞行的 section 立刻落到 committed 位置（不会卡在中间）。
* **被回收的 section**：只有还在物化集合里的 section 参与过渡；新进窗口的 section 直接出现在
  自己的 committed 位置，不做"从屏幕外飞入"。
* **请求是一次性的**：`setSectionMoveAnimated(true)` 只对紧接着的那次换序生效；没有换序时它会在
  下一次 relayout 被丢弃，不会残留到后来的某个变化上。视图在 `moveColumn(..., Animate)` 前后
  给主表头与所有 pane 表头各设置/清除一次，所以"给 A 的请求"不会落到 B 上。
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
  * 工具栏"移动一列"按钮（默认 300 ms、OutCubic 过渡）；
  * `--animation <ms>` 改时长；
  * `--move-demo <png>` 无人值守演示：1200 ms 慢速过渡 + 途中截图，能直接看到 section 在飞、
    body 已经在终点位置。
  * `--drag-demo <png>` 无人值守拖动演示：合成"按下 → 拖过几列"，在拖动途中截图（此时表头只显示
    预览、committed 几何与 body 都没动），然后按 Esc 取消。
