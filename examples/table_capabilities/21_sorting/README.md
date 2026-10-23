# 21 排序与代理模型

通过代理模型排序过滤，视图只发起排序，数据顺序由 model 管理。

筛选只匹配首列“条目名称”的显示文本。勾选“仅显示条目名称包含 1 的行”后保留名称包含 `1` 的条目，取消后恢复全部 120 行；状态栏显示筛选后的行数。首列不包含固定的列号文字，避免所有行都因“列 1”而匹配。

源码相对路径：`examples/table_capabilities/21_sorting/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 21`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_sorting::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
