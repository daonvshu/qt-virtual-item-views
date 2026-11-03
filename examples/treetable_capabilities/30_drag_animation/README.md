# 30 节点拖放与位移动画

同视图单节点 Move 使用单行让位动画，起拖立即收起源子树，数据随整棵子树移动。复制、多节点、放入节点等场景回退指示器，取消拖动后源节点保持收起。

入口：[example.cpp](example.cpp)。主要接口：`setDragFeedbackMode`、`setDragReorderAnimationDuration`、`setDragReorderAnimationEasingCurve`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 30`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
