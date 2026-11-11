# 33 覆盖编辑器与编辑触发

源码：`examples/table_capabilities/33_editors/example.cpp`；布局：`editors.ui`。
在统一程序选择第 33 节，或运行 `table_capabilities --example 33`。

- 第 1 列：单击释放后进入编辑，也支持 F2。
- 第 2 列：使用视图默认的双击与 F2；编辑器演示四周留白。
- 第 3 列：单元格按钮通过 `indexForWidget()` 查询当前绑定，再调用 `edit(index)`。 原始整数存于 `Qt::UserRole + 1`，绑定时格式化为“数量：10 件”，编辑时使用 `QSpinBox`。
- 第 4 列：选中后点击页面上的外部编辑按钮；禁用自动触发仍允许显式调用。
- 第 5 列：单击触发 `editRequest()`，打开业务 Popup，内部显示三种坐标；其他列返回 false，回退到覆盖编辑器。
- 最后一行：model 移除 `ItemIsEditable`，所有入口都拒绝编辑。

`WidgetAdapter` 和 `CellWidgetAdapter` 共用 `ItemEditorAdapter` 回调，行控件模式的
`TableWidgetAdapter` 也可以重载相同函数。默认 `createEditor()` 返回空，不影响既有 adapter。

`createEditor()` 返回的控件直接覆盖在单元格上，无需 stacked layout 或显式 pin。
默认 `setEditorData()` / `commitEditorData()` 使用编辑器的 Qt USER property 与 `Qt::EditRole`；
组合输入框可重载这两个函数，并通过 `setFocusProxy()` 指向主输入控件。
`updateEditorGeometry()` 默认使用完整单元格矩形，可重载为带内边距的矩形。

第 3 列重载 `setEditorData()` 从 `ValueRole` 读取原始整数，重载 `commitEditorData()`
先调用 `QSpinBox::interpretText()` 应用当前输入，再通过 `model->setData(index, value, ValueRole)`
写回。该列不存储 DisplayRole/EditRole；格式化只发生在 `bindCellWidget()` 中。
其他列继续演示默认 EditRole 读写与空文本校验。

编辑事件复用 `QStyledItemDelegate`：Enter 提交关闭，Esc 取消，Tab / Shift+Tab 提交并前后导航，
失焦按 Qt 的规则处理。多行文本框保留原生换行与 Tab 行为。`QLineEdit` 的 validator/fixup
遵循 Qt 默认规则；业务校验应在 `commitEditorData()` 返回 false，此时保留编辑器。
组合控件需要在该回调中验证自己的子输入框。

`commitEditor()` 用于显式提交并关闭，返回是否成功；`closeEditor()` 用于取消。
示例的提交、取消按钮使用 `Qt::NoFocus`，避免点击按钮前的失焦先触发提交。
`editingIndex()` 和 `editorWidget()` 用于查询当前编辑会话。

编辑器独立于展示控件的回收，滚动时保持草稿并裁剪到对应 pane；目标重新可见后仍是同一个编辑器。
切换模型/adapter、模型重置、布局重排、删除/移动行列会取消编辑；树节点被折叠到不可见时也会取消。
外部 `dataChanged` 更新展示内容，但不会重新加载正在输入的草稿。`destroyEditor()` 默认使用
`deleteLater()`，回调不应再次提交数据；销毁时的 context 携带初始行数据和当前有效（或已失效）的索引。

## 自定义编辑请求与 Popup

`editRequest(const EditRequestContext &request)` 在权限检查、目标定位和之前的覆盖编辑提交后调用。
返回 false 继续 `createEditor()`；返回 true 表示请求已由 adapter 接管，不创建框架编辑器。
单击、双击、F2、格内按钮和外部 `edit(index)` 都使用这一入口。
同一个目标已打开时不重复调用请求回调；覆盖编辑器会恢复焦点。

| 字段 | 含义 |
| --- | --- |
| `binding` | 当前索引、行数据、逻辑列和节点深度 |
| `view` / `viewport` | 本次请求对应的视图和 viewport 指针 |
| `viewportRect` | 相对 viewport 的完整单元格矩形 |
| `viewRect` | 相对整个 view 控件的同一矩形，包含表头等偏移 |
| `globalRect` | 全局屏幕坐标中的同一矩形，可用于 Popup 的 move() |
| `finish` | 结束本次业务编辑会话的可复制回调，不写数据、不触发取消 |

矩形是请求时的快照，均为 Qt 逻辑像素，不需要乘 DPI 比例，也不会裁剪到可见区域。
Popup 跨越异步事件时应自行保存 `QPersistentModelIndex` 和 `QPointer`，不要持有普通索引或展示控件。

第 5 列的 `PopupEditor` 使用独立 `popup.ui`，在单元格下方显示，屏幕下方不足时移到上方，
并限制位置在屏幕可用区域内。确认时自行调用 `model->setData()`；取消、Esc、点击外部不提交。
adapter 重载 `cancelEditRequest()`，统一 reject 弹窗。框架在滚动、模型重置/布局或行列结构变化、
更换模型/adapter、切换编辑目标、目标隐藏/失去编辑权限、定位矩形变化、视图或祖先隐藏/移动/缩放、
视图禁用和应用失活时通知取消。外部 `closeEditor()` 也会走该回调。
Popup 无需自己监听模型、滚动条或视图刷新。取消回调可能在请求中的嵌套事件循环触发，
应使用延迟删除；示例设置 `WA_DeleteOnClose`。

Popup 的 `finished` 和 `destroyed` 信号调用 `request.finish`，仅结束该次会话。
重复调用或旧弹窗延迟发出信号都不会影响新会话；框架取消时先解除会话，不会递归取消。
Popup 自己负责数据写回、校验与释放，框架不会调用 `setEditorData()`、`commitEditorData()`
或 `destroyEditor()`。`editingIndex()` 返回 Popup 的目标，`editorWidget()` 返回空。
`commitEditor()` 对 Popup 返回 false，使用弹窗内部确认按钮提交；页面取消按钮也支持 Popup。

复制本目录时，给 `example.cpp` 增加调用 `table_editors::createExample()` 的 QApplication 入口，
启用 AUTOUIC 并链接 VirtualItemViews 与 Qt Widgets 即可。
