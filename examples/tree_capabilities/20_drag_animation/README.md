# 20 拖动位移动画

移动节点时周围节点滑动让位，起拖时立即收起源节点，只按主节点的单行高度占位。Automatic 从文字起拖，Manual 从手柄起拖；可调整动画时长、曲线和节点间距。

源码相对路径：`examples/tree_capabilities/20_drag_animation/example.cpp`。

`VirtualTreeView` 默认使用 `DragFeedbackMode::RowMoveAnimation`。`setDragReorderAnimationDuration(ms)` 设置时长，默认 180 ms，0 表示立即让位；`setDragReorderAnimationEasingCurve(curve)` 设置曲线，默认 OutCubic。复制、跨视图、多节点和投放为子项继续使用指示器；展开收起过渡期间也使用指示器。指示器的独立写法见第 19 章。

在此模式下，单节点拖动在生成预览和进入原生拖动循环前立即完成收起，不执行收起动画；取消或放下后保持收起，不自动恢复展开。占位高度为主节点一行（含正常节点间距），整棵子树的数据仍随主节点移动。若业务在拖动中重新展开源节点，则回退到指示器。

位移动画只改变真实控件、分支和分割线的显示位置，模型顺序到放下后才更新。支持在可见父节点之间插入，移动进入屏幕的节点按需物化。离开、拒绝落点、Esc 取消或模型／显示根／展开状态变化时清理位移偏移。原生拖动期间普通 hover 由框架暂停，结束后恢复。

模型负责 MIME 与插入。本章的 `QStandardItemModel` 在 Move 时插入副本，因此设置 `setMoveRemovesSourceRows(true)`；业务模型已经执行实际移动时应保持 false。

子树最后一项之后，同一让位位置可能对应不同层级。只有模型通过 `canDropMimeData()` 同时允许这些层级时，框架才给让位空白增加描边，左边缘按实际目标层级缩进；若父节点同级投放被模型拒绝，则不显示描边。其他位置仍显示普通让位分割线。

跨层级投放恰好落在源节点原位置时，前后两个边界都保留原层级作为参照：最后一个子节点提升为父节点同级，或父节点同级放入前一子树末尾，模型允许时都显示描边。描边使用隐藏源子树后留下的实际空白区域，这个显示判断不会放开原地移动限制。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 20` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_drag_animation::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
