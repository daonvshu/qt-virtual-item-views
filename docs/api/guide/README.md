# API 文档导航

用浏览器打开 [API 首页](../index.html)，无需服务器、网络或额外工具。文档按视图和能力组织，提供公开函数、默认行为、配套设置和独立示例。

## 按需求阅读

1. **选择视图**：在首页比较列表、表格、树、树表、流式表格、瀑布流和换行布局，进入适合业务数据结构的页面。
2. **查找功能**：查看“能力与配套设置”，或搜索 auto height、编辑、拖放、冻结、间距等关键词。搜索同时匹配中英文。
3. **查看接口**：在“公开函数列表”点击签名，跳转到用途、配置约束及相关能力说明。同名重载分别列出，信号单独分组。
4. **参考示例**：通过能力章节链接阅读最小接入代码；模型、adapter、控件和布局位于同一示例目录，运行与复制方式见对应 README。

右上角 **中文 / English** 可切换界面和函数说明，C++ 标识符与签名保持不变。页面会记住语言选择，也可用 `?lang=zh` 或 `?lang=en` 分享指定语言的链接。没有 JavaScript 时显示完整中文内容。

## 继承接口与组合设置

每个视图页列出本类型声明的公开接口。模型、选择、编辑、背景和自适应高度等通用功能还应查看 [VirtualItemView](../pages/virtualitemview.html)；[VirtualTreeTableView](../pages/virtualtreetableview.html) 的列布局和表头功能同时参考 [VirtualTableView](../pages/virtualtableview.html)。Qt 自身提供的接口请查相应 Qt 基类文档。

配置前先阅读能力限制：不同视图的尺寸策略、物化模式和冻结支持并不相同。优先使用框架已有的状态绘制、编辑器生命周期、复用和滚动处理；需要自定义内容时，通过 adapter 或对应扩展接口实现。

## 供业务 agent 查询

将 [Agent 能力索引](../../AGENT_GUIDE.md)作为业务项目中的 VIV 参考入口。建议按“需求关键词 → 声明类型及基类 → 最小示例”的顺序查找，避免重复实现框架已有功能。

[public-api.json](../data/public-api.json) 可按 `headers → types → entries` 定向查询：

- `signature`：完整公开签名，用于区分重载。
- `documentation.zh` / `documentation.en`：中英文函数说明。
- `documentation.notes`：默认值、参数约定和配套限制。
- `documentation.related`：对应能力索引。
- `access` / `symbols`：访问类别及检索符号。
- `header` / `line`：头文件路径与声明位置，便于进一步查看接口。

所有示例和头文件路径均相对于 VIV 源码根目录；在业务项目中按依赖所在目录解析。
