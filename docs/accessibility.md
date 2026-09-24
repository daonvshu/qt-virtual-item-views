# Accessibility（§37）

虚拟化带来一个硬约束：**逻辑行不是 QObject**。`QListView`/`QTreeView` 的辅助功能之所以能工作，
是因为 `QAccessibleWidget` 能按需生成"第 i 个可见项"的接口；而我们的行是真实 QWidget，但只在
可见区内存在，滚出窗口就被回收了。所以这里走"**虚拟节点**"路线：节点按需创建，只覆盖**当前可见
的那一屏**，文本/状态/几何都在被问到的时候从 model 与已提交的布局里现取。

## 1. 接入

```cpp
#include <virtualitemviews/accessibility.h>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    viv::installAccessibilityFactory();      // 幂等；应在创建视图之后、任意时候调用
    ...
}
```

工厂函数 `viv::createAccessibleItemViewInterface()` 可以直接交给
`QAccessible::installFactory()`（签名就是 `QAccessible::InterfaceFactory`），
`installAccessibilityFactory()/removeAccessibilityFactory()` 只是成对的便捷封装。
不安装也能跑，只是屏幕阅读器看到的是 `QWidget` 的通用接口（一个空的滚动区域），看不到任何行。

产品里真正的接入点是平台插件：Windows 上是 `qwindows.dll` 里的辅助功能实现，它会在
`QAccessible::queryAccessibleInterface()` 时按安装顺序询问所有工厂 —— 我们安装的工厂对
`VirtualItemView` 子类返回自己的接口，对其它对象返回 `nullptr`，因此不影响 `QTreeView` 等其它控件。

## 2. 暴露了什么

| 节点 | role | children | parent |
| --- | --- | --- | --- |
| 视图（List/Tree/Table） | `List` / `Tree` / `Table` | **当前可见的顶层行**（表格：行；树：顶层节点） | 父控件的接口 |
| 列表项 | `ListItem` | — | 视图 |
| 树的节点 | `TreeItem` | 展开时它的**可见子节点**，折叠/叶子为 0 | 父节点（顶层节点是视图） |
| 表格行 | `Row` | 该行**可见的列**上的 cell | 视图 |
| 表格单元格 | `Cell` | — | 所在行 |

行来自当前 materialization 模式：Row Widget Mode 取 materialized row，Cell Widget Mode
（§28，表格没有行控件）取 materialized cell 再按行去重，两种模式对上层完全一样。

`text()` 全部来自 model：`Name` = `DisplayRole`，`Description` = `ToolTipRole`（回退
`StatusTipRole`），`Help` = `StatusTipRole`，`Value` = `EditRole`；视图自身的 `Name` 取
`accessibleName()`（回退窗口标题），`Description` 是 "可见 / 逻辑" 的条数，例如
`8 / 100000 items visible`。`setText()` 是空实现：数据属于 model，辅助层从不回写。

`rect()` 用已提交的布局：行取自 `visualRect()`，单元格再按 `columnGeometry()` 收窄到列上
（冻结列用它自己的视口 x），最后换算到屏幕坐标。因此滚动、改列宽、拖列、冻结都不会让
辅助功能的位置与实际画面对不上。

`state()`：`selected`/`active`/`focused` 来自 `QItemSelectionModel` 与 `hasFocus()`，
`selectable`/`disabled` 来自 model 的 item flags，`expandable`/`expanded`/`collapsed` 来自树的
展开状态，`offscreen` = 行不在视口窗口内（滚出去了），`invisible` = 列被隐藏等"根本画不出来"
的情况。

## 3. 动作与事件

`actionInterface()`（`QAccessibleActionInterface`）提供：

| 节点 | 动作 | 行为 |
| --- | --- | --- |
| 视图 | `setFocus` | `view->setFocus()` |
| 视图 | `scrollUp/Down/Left/Right` | 按像素滚动（±32 px），表格才有左右 |
| 行/项 | `setFocus` | 设为 current 并聚焦视图 |
| 行/项 | `press` | 树的节点展开/收起；其它情况等价于一次点击 |

`press` 走的是 `VirtualItemView::activateIndex()`：设置 current 后发出 `clicked()` 与
`activated()` —— 与鼠标双击完全同一条业务代码路径，应用不需要为辅助功能写第二个入口。

事件的来源是 `AccessibilityNotifier`（随接口创建，跟踪 model 与 selection model 的替换）：
current 变化发 `QAccessible::Focus`，选择变化发 `QAccessible::Selection`，model 的
`modelReset`/`rowsInserted`/`rowsRemoved`/`dataChanged` 发
`QAccessibleTableModelChangeEvent`。没有辅助工具在监听时（`QAccessible::isActive()` 为假）
这些事件是空操作，所以框架的热路径不会被拖慢。

## 4. 边界与取舍

* **只暴露可见行**：一百万行的列表暴露的是当前视口那几行（外加屏幕阅读器访问过的节点），
  逻辑总数在 `Description` 里（"N / M items visible"）。这也是为什么节点数不随逻辑行数增长 ——
  与框架其他部分同一条不变量。
* **节点的生命周期**：节点由视图接口持有，`QPersistentModelIndex` 作为身份。行被删掉之后，
  已经交出去的节点会返回 `isValid() == false` / `invalid` 状态，而不是变成悬垂指针。
* **没有 `QAccessibleTableInterface`**：行列语义用 `Row` + `Cell` 角色表达（`childCount` 即
  "可见列数"），没有实现 IAccessible2 的表格接口；如果将来需要屏幕阅读器的"按行列朗读"，
  那是 v1.x 的扩展点。
* **没有文本/编辑接口**：`TextInterface`/`EditableTextInterface` 未实现（行内编辑器自己会
  暴露接口，因为它们是真实 QWidget）。
* **平台相关**：Windows 的 UIA / macOS 的 VoiceOver 通过各自的平台插件走同一条
  `QAccessibleInterface` 路径；Linux 上 AT-SPI 亦然。测试里用
  `QAccessible::queryAccessibleInterface()` 直接断言，不依赖平台插件是否激活。

## 5. 测试

`tests/unit/tst_accessibility` 覆盖：三种视图的 role、可见行按需生成（10 万行 < 100 个节点）、
文本/状态/矩形来自 model 与布局、current 作为 `focusChild()`、`childAt()` 命中、
表格行列语义（含隐藏列、冻结列、Cell Widget Mode）、树的层次（折叠/展开、parent/child 往返）、
`press` 等价点击、以及 model 结构变化后节点跟着更新。

```bash
cmake-build-debug-qt6/tests/tst_accessibility        # 需要 QT_QPA_PLATFORM=offscreen（CTest 已设置）
```
