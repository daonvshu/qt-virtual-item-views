# 11 状态关联控件

鼠标悬停或选中行时显示第 3 列操作按钮，业务数据与视觉状态分开。

仅第 3 列通过 `setRetainSizeWhenHidden(true)` 保留隐藏按钮的位置，避免按钮显隐时文字跳动。其他列不保留按钮占位，让文字使用完整的内容宽度。控件可能跨列复用，因此每次 `bindCellWidget()` 都按当前列重新设置此属性；按钮是否显示由 `visualStateChanged()` 更新。

源码相对路径：`examples/table_capabilities/11_state_content/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 11`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_state_content::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
