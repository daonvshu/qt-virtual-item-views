# 38 覆盖编辑器与 Popup

源码：`examples/treetable_capabilities/38_editors/example.cpp`；页面：`editors.ui`；弹窗：`popup.ui`。
运行 `treetable_capabilities --example 38`，或在统一程序中选择本章。
本目录独立定义 model、adapter、展示控件与 Popup，不引用其他章节代码。

使用 `CellWidgets` 和 `CellWidgetAdapter`，五列依次演示：

1. 单击进入文本编辑，也支持 F2。
2. 双击/F2 进入文本编辑，`updateEditorGeometry()` 演示可选覆盖范围。
3. 子控件按钮通过 `indexForWidget()` 查询当前绑定，再调用 `edit(index)`。
   `QSpinBox` 从 `Qt::UserRole + 1` 读取整数并写回；展示时才格式化为数量文本。
4. 只允许通过页面外部按钮调用 `edit(currentIndex())`。
5. 单击走 `editRequest()`，打开自定义 Popup，并显示 viewport/view/global 三种定位矩形。

模型用普通 `QStandardItemModel` 填充；只读项移除 `ItemIsEditable`，所有入口均尊重该权限。
`setEditTriggers()` 设置默认手势，adapter 的 `editTriggers()` 覆盖每项策略。
`createEditor()` 创建覆盖编辑器；默认 USER property 与 `Qt::EditRole` 读写可按需重载，
不需要 stacked layout 或手动 pin。空文本在 `commitEditorData()` 中返回 false，保留草稿。

Enter 提交、Esc 取消、Tab/Shift+Tab 前后导航使用框架的 Qt delegate 流程。
`commitEditor()` 提交覆盖编辑器；`closeEditor()` 取消当前编辑（包含 Popup）。
Popup 自行确认写回，`commitEditor()` 对 Popup 返回 false。
外部提交/取消按钮使用 `Qt::NoFocus`，避免点击前的失焦先提交。

`editRequest()` 返回 false 时回退到覆盖编辑器，返回 true 时由业务加载和提交弹窗。
框架通过 `cancelEditRequest()` 通知关闭，Popup 不自行监听模型、滚动条或视图布局。
Popup 的 `finished`/`destroyed` 调用可复制的 `request.finish`，只结束本次会话，
不会误关后来打开的编辑器。`editingIndex()` 包含 Popup 目标，`editorWidget()` 对 Popup 返回空。
示例使用 `QPersistentModelIndex`/`QPointer` 保存异步生命期内的索引和对象，`WA_DeleteOnClose` 延迟释放弹窗。

父节点是只读分组，子节点才进入编辑；双击分组保持展开/收起行为。
行号相对父节点，按钮和编辑器始终使用完整 QModelIndex，不把可见行号当成模型行号。
“收起全部”通过视图更新可见映射，框架取消被隐藏子节点的编辑；再次展开后重新编辑即可。

复制整个目录，在自己的 QApplication 入口调用命名空间中的 `createExample()`；
CMake 启用 AUTOUIC 并包含两个 `.ui` 文件，链接 VirtualItemViews 与 Qt Widgets。

编辑范围下拉框演示 `context.indentationWidth`（祖先缩进）和 `context.branchWidth`（当前节点 branch 格）
的三种组合：整行/整格、排除祖先缩进、排除祖先缩进和 branch。示例默认只覆盖内容；
框架默认编辑器仍使用完整矩形。先选择范围再打开编辑器，`editArea()` 中的写法可以直接复制。
Popup 也通过 `request.binding` 读取这两个字段，并显示其数值。

两项都是 Qt 逻辑像素、裁剪前的布局占位，不是图标的绘制尺寸。深层节点或窄列应将偏移限制在矩形宽度内。
隐藏 branch 区域会令 `branchWidth` 为 0，祖先缩进保留；仅隐藏图标仍保留 branch 占位。
TreeTable 仅逻辑第 0 列提供这两个宽度，其余列都为 0；列换序不会改变该规则。
