# 19 节点拖放与手动起拖

树拖放：行上下边缘插入兄弟节点，中间投放为子节点；Automatic 从文字起拖，Manual 从手柄起拖。

源码相对路径：`examples/tree_capabilities/19_drag_drop/example.cpp`。

本章显式设置 `setDragFeedbackMode(VirtualTreeView::DragFeedbackMode::Indicator)`，使用插入线与目标框区分兄弟插入和成为子项。位移动画及时长、曲线配置见第 20 章。权限由 model flags、MIME 和 drop 接口控制。原生拖动期间框架暂停普通 hover，结束后恢复，边缘自动滚动继续生效；Esc 可取消。手柄按下位置通过 `DragOptions` 传给框架。

插入线的左端对齐目标层级的内容缩进。插入展开父节点的同级时，指示线绘制在其整个可见子树之后；与插入最后一个子节点的同级共用同一纵向边界，通过左端缩进区分层级。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 19` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_drag_drop::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
