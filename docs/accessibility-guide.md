# 可访问性与运行诊断

## 可访问性

在创建视图前注册辅助功能工厂：

```cpp
#include <virtualitemviews/accessibility.h>

viv::installAccessibilityFactory();
```

工厂按需给可见列表项、树节点、表格行和单元格提供接口；文本、状态与几何取自当前模型和视图。表格还暴露行列及单元格语义，支持 current、选择及 press/focus/scroll 等动作。虚拟节点暂不提供文本和可编辑文本接口；行内真实 QWidget 自身的编辑接口仍由该控件提供。

## 查看虚拟化效果

```cpp
#include <QDebug>

const viv::VirtualViewStats stats = view.stats();
qDebug() << "logical" << stats.logicalItems
         << "visible widgets" << stats.materializedItems
         << "pooled" << stats.pooledWidgets
         << "pinned" << stats.pinnedWidgets;
view.setLifecycleLoggingEnabled(true);
const QStringList events = view.lifecycleLog();
```

`stats()` 还包含创建/绑定/回收计数。生命周期日志有界且默认关闭；定位控件数量持续增长时先检查 pin 和 adapter 的解绑逻辑。

固定高度或仅有少量尺寸例外的模型无需为每个逻辑行保存控件或完整尺寸状态。`logicalItems` 很大而 `materializedItems` 仍接近视口容量时，说明虚拟化窗口按预期工作。

## 性能探测

所有视图继承 `VirtualItemView` 的性能开关，默认关闭：

```cpp
#include <QLoggingCategory>

view.setObjectName(QStringLiteral("recordsView"));
view.setPerformanceLoggingEnabled(true);

// Merge these category rules into the application's existing logging rules.
QLoggingCategory::setFilterRules(QStringLiteral(
    "viv.performance.refresh.debug=true\n"
    "viv.performance.paint.debug=true"));
```

使用 `QT_LOGGING_RULES` 配置同名规则也可以。两个分类都用 `qCDebug` 输出，可独立筛选；关闭分类会跳过该分类的计时和采样，关闭视图开关会停止所有性能采样。开启视图开关时，分类默认允许 debug 输出，仍以应用最终的分类规则为准。`isPerformanceLoggingEnabled()` 查询视图开关。

未开启时，探测点仅执行轻量的开关检查，跳过计时、性能计数、统计快照、汇总和日志准备；回调直接执行原有业务逻辑。所有视图都关闭探测时，子控件探测点也跳过所属视图的父对象查找。开关可在运行中调整，关闭会丢弃尚未输出的统计窗口。`stats()` 原有的虚拟化基础计数与性能探测独立，继续保留。

两个分类均按视图使用约 1 秒的统计窗口。刷新、模型通知、表头布局、状态更新和绘制过程中只累计数据，不逐次打印；定时器到期后，每个视图、每个分类各输出一份多行摘要，整份只调用一次日志处理器。没有新采样时不打印；关闭视图开关会丢弃尚未输出的窗口，视图销毁时也不会强制输出。

摘要先显示窗口耗时、控件数量和创建／绑定／回收增量，再按累计耗时降序列出最耗时的 8 个阶段。计数单独放在 `Counters` 表中，不再与耗时混排。时间统一用毫秒、小数位固定，数字列对齐；`Slowest stage call` 单独指出单次最慢的阶段，即使该阶段没有进入累计耗时前 8 名也能看到。

下面是精简的输出示意，数字仅用于说明格式：

```text
[viv perf #18] refresh | viv::VirtualTableView | name="recordsView"
  Window: 1002.3 ms | Measured: 137.805 ms | Samples: 26 | Avg: 5.300 ms | Max: 19.231 ms
  Widgets: 336 materialized / 10000 logical | Pool: 84 | Pinned: 0
  Activity: created=0 | bound=147 | recycled=147 | Overscan: 2/4 | Auto measure: off
  Hot stages: 3/3, sorted by total (inclusive; nested stages overlap)
    Stage                              Total ms  Share %      Calls     Avg ms     Max ms
    refresh.relayout                    100.000     72.6         26      3.846     19.231
    cells.materialize                    32.306     23.4         26      1.243      6.100
    callback.cell.bind                   17.627     12.8        147      0.120      0.500
  Slowest stage call: refresh.relayout (19.231 ms)
  Counters (window totals):
    cells.geometry-changed                  180
    cells.geometry-unchanged               4832
```

需要查看所有阶段时，额外启用 `viv.performance.details.debug=true`。该分类默认关闭；启用后，每个摘要还会对应一份完整耗时表，以同一个 `[viv perf #编号]` 关联。它复用现有采样，只有输出时才额外整理完整表格。原有 `viv.performance.refresh` / `viv.performance.paint` 仍控制对应分类是否采样，仅开启 details 不会启用已被过滤的采样分类。使用 `viv.performance.*.debug=true` 会同时开启明细；只想看摘要时可以显式设置 `viv.performance.details.debug=false`。

| 日志阶段 | 包含的工作 |
| --- | --- |
| `refresh.*`、`model.*` | 刷新请求、可见范围、索引匹配、数据通知处理与行数据上下文 |
| `items.*`、`cells.*` | 行或单元格物化、复用、绑定、解绑与回收 |
| `cells.widget.*`、`cells.layout.clip-hosts` | 单元格控件几何、显隐、层级、屏外移动与冻结裁剪容器同步 |
| `recycler.*` | 回收时的隐藏、焦点清理、换父控件与复用池处理 |
| `layout.*`、`header.*` | 尺寸测量、行列几何、冻结区域、表头与动画布局 |
| `grid.*` | 行分割线、列间距、冻结边界普通线与区域掩码更新 |
| `state.*` | hover / selected 状态通知和表头同步 |
| `tree.*`、`tree-table.*`、`flow.*`、`masonry.*` | 树展开折叠与可见性、流式布局、瀑布流重排 |
| `callback.*` | 外部 adapter、间距工厂与绑定器、合并提供器、分支绘制及刷新信号的同步响应 |

业务 `create`、`widgetType`、`bind`、`unbind`、`visualStateChanged`、行布局与尺寸估算分别计时。控件的 `sizeHint()` / `heightForWidth()` 包含在 `callback.widget.measure` 中；业务在回调里查询模型、构建子控件、设置样式、触发布局或同步信号的时间全部计入回调。框架直接查询模型和计算索引的时间包含在相应刷新、上下文和可见性阶段中。

`cells.widget.set-geometry`、`show`、`hide`、`raise`、`reparent` 和 `move-offscreen` 按实际控件操作分别统计；`recycler.hide`、`clear-focus`、`reparent` 和 `pool-return` 拆分回收步骤。这些阶段包含相应 Qt 调用同步触发的业务 `resizeEvent()`、事件过滤器、样式更新和信号响应；不应将它们误认为只有框架计算。`layout.horizontal-scroll` 汇总横向滚动处理，`scroll.cell-refresh`、`scroll.cell-refresh-deferred` 和 `scroll.geometry-only` 分别计数立即物化刷新、重入时排队刷新及窗口未变的位置更新，显示在 `Counters` 表中。全部仍在原有统计窗口汇总。

单元格模式使用固定裁剪区域中的可平移内容层处理滚动。`cells.layout.content-layer` 和 `cells.layer.set-geometry` 统计内容层更新；`cells.layer-moved` 记录实际改变内容层几何的次数，`cells.layer-rebased` 记录长距离滚动或跳转后重设坐标基点的次数。纯滚动时，已有单元格通常计入 `cells.geometry-unchanged`，新进入、尺寸变化、动画偏移或坐标基点重设后的单元格仍通过 `cells.widget.set-geometry` 更新。比较优化效果时，应同时观察内容层更新、单元格更新与刷新总耗时。

`viv.performance.paint` 在同样的时间窗口中汇总框架绘制。各部分包括视图底色和状态背景、表头背景／内容／前景、行列间距、普通网格线、冻结分隔线、拖动指示器、树分支和圆角边框合成。同一视图重复执行的部分合并为调用次数、累计耗时、平均耗时和单次最大耗时。如果业务回调嵌套事件循环，定时器到期时仍有计时作用域未退出，会等这些作用域完成后再输出。

`Window` 是窗口实际经过的时间；`Measured` 只累计该视图、该分类最外层计时作用域的执行时间，不包含作用域之间的空闲等待，也不重复累计同分类的嵌套作用域。`Samples` 是这些最外层作用域的次数，不能当作帧数；概况中的 `Avg`、`Max` 分别是其平均和最大耗时。

耗时表的 `Total ms` 包含阶段内部调用；`Share %` 是该阶段累计耗时占 `Measured` 的比例，用于比较热点，不是独立分账。阶段 `Avg ms` 按该阶段的 `Calls` 计算，`Max ms` 可用于定位某一次特别慢的回调。父子阶段以及发生同步嵌套的两个分类不能相加作为总时间；递归进入同名阶段时，其累计值甚至可能超过 `Measured`。`Counters` 是整个窗口的累加次数／数量，例如 `cells.retained` 是每次采样的保留数量累加，应与结束时的物化数量区分。

`Activity` 中的 `created`、`bound`、`recycled` 是该窗口期间的计数增量；`Widgets` 中的物化数、逻辑项数、`Pool`、`Pinned` 是结束时的数量；`Overscan` 的前／后项数和 `Auto measure` 是窗口开始时的配置。绘制汇总的计数增量也可能包含输出前发生的刷新，不代表这些对象由绘制直接创建。

排查接入开销时，可先看这些信号：

- 稳定滚动时 `created` 持续增加：检查 widget 类型是否过多、复用池配置及屏外 pin。
- `callback.*.bind` 次数明显偏多：检查是否重复设置 model / adapter、发送整表 `dataChanged` 或调用 `refreshItemData()`。
- `refresh.requests` 很多：同一排队刷新之前存在多个失效请求；它们会合并，但请求前的业务工作仍可能重复。
- `refresh.reentrant-skipped`、`refresh.stale-pass`：回调中重新刷新或修改模型使当前操作重入、失效；`cells.materialize` 在单元格模式中的耗时和次数可辅助定位。
- `measure.height-changed`、`measure.convergence-limit`：动态测量改变了高度，或高度反复变化触及收敛保护；检查尺寸提示是否稳定以及列宽变化是否引发多轮布局。
- 表头、间距、合并或分支阶段偏慢：结合回调次数和 `Max ms` 检查是否反复遍历全模型，或在绘制中执行昂贵查询。

此开关统计当前视图调用栈内的同步工作，不是整帧 CPU / GPU 分析器。业务 QWidget 独立收到的 `paintEvent()`、排队信号、网络请求和工作线程不会自动归入发起它们的回调；默认表头的虚拟绘制钩子和分支 renderer 在框架调用期间会被统计。已有圆角边框效果的 `paint.frame-composite` 包含其同步渲染子控件的时间，但不会为了探测而安装额外的图形效果或重发绘制事件。开启探测和写日志本身有开销，定位完成后应关闭。
