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
  摆在两者之间的位置。它不写回 `HeaderGeometry`。
* 渲染器可以把"我现在画在哪儿"报告出来（`sectionVisualX()`），并在每一帧通知视图
  （`setVisualGeometryCallback()`）。**只有列控件的 x** 会取自这份视觉几何：
  列宽、所在 pane、span、裁剪、命中测试、滚动条全都继续读 committed geometry。

换句话说，body 的**几何**仍然只在 commit 那一次重排；跟帧的只是"这一列现在画在哪个 x"。

## 2. 哪些情况动画、哪些必须立刻

| 交互 | committed 是否变化 | 表头 | body |
| --- | --- | --- | --- |
| Hover / sort icon / badge | 否 | 业务控件自己的动画 | 不动 |
| **Section move（拖动 `VirtualHeaderView` 的列）** | 只在松手时变一次 | 拖动期间是**视觉预览**：被拖的列跟随指针，邻居**平滑**让出/合上插入位，松手提交后过渡收敛 | 整列跟着 section 走（可关，见 §3） |
| **Section move（换序，`MoveAnimation::Animate`）** | 是，且立刻最终 | **视觉过渡**（默认 300 ms，OutCubic） | 整列跟着 section 走（可关，见 §3） |
| Section move（换序，程序化 / 默认 `Immediate`） | 是，且立刻最终 | 立刻 | 立刻 |
| Resize（拖列宽） | 是，逐帧 | 立刻 | 逐帧 |
| Hide / show | 是 | 立刻 | 立刻 |
| 横向滚动 / pane 变化 | 否 | 立刻（否则会与 body 撕裂） | 逐帧 |

判据只有一条：**committed 几何会不会跟着用户的手变**。会，表头就必须逐帧跟上（resize、滚动、
pane 变化）；不会（换序是唯一一种"先定终局、再让人看到过程"的交互），才把过程交给渲染器 ——
而且**只有被明确请求的换序才播**：程序化设置列顺序（`moveColumn()` 默认、模型换序、状态恢复）
不应该变出一个没人要求的动画。

实现上靠"视觉顺序是否变化"来区分：`VirtualHeaderView::relayout()` 比较本次与上次的可见列顺序，
顺序变了且列集合没变（不是隐藏/插入/删除）、并且收到了一次性请求（`setSectionMoveAnimated(true)`）
才启动过渡，所以 resize / hide / 滚动 / 程序化换序都不会误触发。

拖动与过渡这两种过程都只动渲染器的视觉几何，而**整列跟不跟**由视图的设置决定（下一节）：
默认跟着一起走，因为"表头在飞、body 已经跳过去"读起来像撕裂；`setColumnFollowsHeaderVisual(false)`
回到"commit 时一次到位"的旧行为。

## 3. API

```cpp
// 视图级（默认：启用，300 ms，OutCubic；0 表示关闭）
view.setHeaderAnimationEnabled(true);
view.setHeaderAnimationDuration(240);

// 整列跟着表头的视觉几何走（默认开）：拖动时整列跟着指针，松手后一起收敛
view.setColumnFollowsHeaderVisual(true);

// 程序化换序：默认即时；需要过渡时显式请求（只影响表头，committed 几何立刻生效）
view.moveColumn(1, 4);                                   // 立刻
view.moveColumn(1, 4, VirtualTableView::MoveAnimation::Animate);

// 渲染器级（CustomRenderer 也可以自己实现）
header->setSectionAnimationEnabled(true);
header->setSectionAnimationDuration(240);
header->setSectionMoveAnimated(true);   // 一次性请求，被下一次 relayout 消费

// 渲染器级：视觉几何的**报告**接口（默认实现 = "我没有视觉几何"）
bool sectionVisualX(int logicalIndex, int *viewportX) const;     // 现在就画在这个 x
bool hasVisualSectionGeometry() const;                          // 有没有在拖动/过渡
void setVisualGeometryCallback(std::function<void()> callback); // 每帧通知视图
```

设置会下发给主表头与每一个 pane 表头（§43），也会在安装新表头 / 新建 pane 表头时应用。

`setColumnFollowsHeaderVisual()` 只影响**框架摆放的列控件**（Row Widget Mode 的 `ColumnHost`、
Cell Widget Mode 的 cell 控件与 span）。两点要知道：

* adapter 的 `layoutRowWidget()` 是"每次变化调一次"的钩子，动画帧不会重跑它（一帧一次业务布局
  代价太大），所以自己摆放子控件的 adapter 不跟帧 —— 用 `ColumnHost` 就有。
* 跟帧只改 x：列宽、pane、span、裁剪、`columnGeometry()`、命中测试、滚动条全程都是 committed 值。
  隐藏列 / 改冻结这类 pane 归属变化不做动画（直接落到 committed），新物化的列也不做"从屏幕外飞入"。

## 4. 渲染器支持矩阵

| 渲染器 | 支持 | 说明 |
| --- | --- | --- |
| `LabelHeaderView`（默认） / `VirtualHeaderView`（§17–§19） | 是 | 每个可见 section 一个控件、位置由渲染器决定，所以能逐帧插值、也能报告视觉 x；打断时从当前位置继续（不会跳回上一帧的起点）。**默认装的就是这一类**，所以动画与"整列一起动"开箱可用。 |
| `NativeHeaderView`（§16，不再是默认） | 否（忽略） | section 的位置与绘制属于 `QHeaderView`，框架不改它的 paint；此时表头与 body 一起在 commit 时跳变（`setColumnFollowsHeaderVisual()` 在这边无事可做）。 |
| 自定义 `HeaderViewInterface` | 可选 | 实现 `setSectionAnimationEnabled()/setSectionAnimationDuration()` 即参与过渡；再加上 `hasVisualSectionGeometry()/sectionVisualX()/setVisualGeometryCallback()` 就能让整列跟着走。都不实现则语义与 native 相同。 |

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
* **拖动中的 pane**：冻结 pane 有自己的渲染器（§31/§43），拖动发生在**它**上面。视图会把每个
  pane 渲染器的视觉几何都问一遍，所以冻结 pane 内的拖动同样带动整列。
* **关掉"整列一起动"的中途**：`setColumnFollowsHeaderVisual(false)` 会立刻把列控件放回 committed
  位置（正在飞的 section 继续飞完自己的过渡，只是 body 不再跟）；再打开则从下一帧重新跟上。
* **一帧只重排 x**：跟帧只重新摆放框架管理的控件，不重跑 adapter 钩子、不创建/回收控件，
  代价与"横向滚动一步"同阶（100 列 x 26 可见行，Debug 约 0.2 ms/帧）。

## 6. 测试与演示

* `tests/unit/tst_virtualheaderview`：
  * `sectionMoveAnimatesWhileTheCommittedGeometryStaysAuthoritative`：换序后 committed 立刻是终值，
    section 控件仍在途中，最终落到 committed 位置。
  * `resizeAndDisabledAnimationStayImmediate`：resize 不产生过渡；关闭动画后换序立刻生效。
  * `tableForwardsTheAnimationSettings`：视图级设置下发到渲染器，端到端验证 committed/visual 分离。
  * `bodyFollowsTheSectionsWhileAMoveAnimates` / `bodyFollowsTheDragPreviewAndTheCommit`：换序与拖动
    期间每一帧 `body` 的列控件 x == 表头 section 的 x，committed 几何同时已经是终值；取消（Esc）、
    提交、以及过渡途中来一次 view 自己的重排（resize）都不撕裂。
  * `bodyFollowsAFrozenPaneDrag`：拖动发生在冻结 pane 的渲染器上时同样带动整列。
  * `bodyFollowCanBeTurnedOff`：关掉设置后回到"表头在飞、body 在 commit 时一次到位"。
* `examples/table_custom_header`：
  * 工具栏"移动一列"按钮（默认 300 ms、OutCubic 过渡）；
  * `--animation <ms>` 改时长；
  * `--move-demo <png>` 无人值守演示：1200 ms 慢速过渡 + 途中截图，能直接看到 section 在飞、
    body 的整列一起在飞；报告里 `body0-4` 与 `header0-4` 两条相同即"没撕裂"。
  * `--drag-demo <png>` 无人值守拖动演示：合成"按下 → 拖过几列"，在拖动途中截图（此时表头只显示
    预览、committed 几何一个字节都没动，但整列已经跟着走了），然后按 Esc 取消。
  * `--drag-commit-demo <png>`：同上但**松手提交**，在提交后的过渡途中截图。
  * 工具栏"整列一起动"复选框 / `--no-body-animation`：切换 `setColumnFollowsHeaderVisual()` 做对照。

### 库是怎么做的

应用侧**不需要写任何代码**：库自己把渲染器报上来的视觉几何接进列定位。`HeaderViewInterface` 的
三个钩子（§3）是唯一的接缝；`VirtualTableView` 在 `applyColumnLayout()` / `updateCellGeometry()` 里
用 `columnVisualX()` 覆盖列 x，并在渲染器的每一帧回调里只重排这些控件：

```text
渲染器（VirtualHeaderView）
  positionSections() 是唯一的落点：拖动预览、让位补间、换序过渡都经过它
    └► notifyVisualGeometry()：在拖动/过渡中每帧回调，落到 committed 的那一帧也回调一次
       （空闲时一句话都不说，所以不动的表头零开销）

视图（VirtualTableView）
  安装表头时 setVisualGeometryCallback([this]{ onHeaderVisualGeometryFrame(); })
    └► 每帧只做 updateVisualColumnGeometry()：重新摆放框架管理的列控件
       （不跑 adapter 钩子、不创建/回收控件）
  真正的定位 pass（applyColumnLayout() / updateCellGeometry()）也问一次 columnVisualX()，
  所以过渡途中的任何重排（滚动、resize、重新物化）落下来就是视觉位置，不会闪回 committed

于是"松手后从预览位置收敛"是白送的：表头自己的过渡就是从预览位置起步的，body 跟着它走；
取消（Esc）或没有真的换序时表头立刻回到 committed，body 同一帧跟着回去。
```

代价：每帧一次"重排可见行 x"，与横向滚动一步同阶（见 §5）。这是**默认开**的原因 —— 拖动一整列
而 body 只跳一下，读起来就是撕裂；想回到旧行为写 `setColumnFollowsHeaderVisual(false)`。
