# 10 缩进与分支配置

调整层级缩进；隐藏图标保留图标格，隐藏分支区域移除自身图标格但保留父子缩进。树表没有树视图的叶子组对齐配置。

入口：[example.cpp](example.cpp)。主要接口：`setIndentation`、`setBranchIndicatorsVisible`、`setBranchAreaVisible`、`setBranchIconSize`、`setBranchIconColor`、`setBranchIndicatorAlignment`、`setBranchIndicatorPadding`、`setExpandedBranchIcon`、`setCollapsedBranchIcon`。关键调用旁有英文注释。

图标便捷接口与 `VirtualTreeView` 一致，仅影响默认 renderer。图标默认大小为 10 个逻辑像素，居中且 padding 为零；可分别调整四边 padding，图标自动缩小到剩余空间。取消“蓝色图标”恢复图标原色或内置三角的调色板颜色。

下拉框演示内置三角、仅展开图标、仅收起图标和双图标。点击分支展开或收起，单图标随展开进度旋转 90°；设置双图标时直接切换。两个图标都清空时恢复内置三角。自定义 renderer 的用法见第 11 章，其绘制不受这些便捷设置影响。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 10`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
