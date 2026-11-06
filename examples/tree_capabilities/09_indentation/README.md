# 09 分支格宽与叶子对齐

缩进：格宽控制每级缩进；关闭叶子缩进后，全叶子兄弟组直接对齐父项内容。

源码相对路径：`examples/tree_capabilities/09_indentation/example.cpp`。

叶子缩进默认 true。关闭时，如果同级中任意节点可展开，该组的叶子仍与它对齐；只有全部为叶子时才省略本行分支格。判断依据是是否有子节点，不是当前是否展开。隐藏图标仍保留缩进，可配合双击、键盘和展开接口操作。

“隐藏 branch 区域”调用 `setBranchAreaVisible(false)`，移除图标及每个节点自身的分支占位，保留 `depth × indentation()` 的父子层级缩进；默认不勾选。“分支格宽”仍可调整层级间距。取消后恢复原来的图标和占位，图标显示状态和叶子缩进配置仍保留。节点仍可通过双击、键盘和展开接口操作。若需要连同全部层级缩进一起移除，可调用 `setIndentation(0)`。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 9` 进入本章。复制本目录即可使用，不依赖其他能力目录。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_indentation::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
