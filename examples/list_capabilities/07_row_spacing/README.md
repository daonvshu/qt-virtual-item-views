# 07 行间距和分割线

示范 setRowSpacing、分割线开关 / 线宽 / 颜色，以及自定义间距 factory + binder。工厂只负责创建，binder 完整刷新复用控件；索引是间距上方的行。线宽按物理像素处理，行高和间距使用逻辑像素。间距太小时自定义内容会被裁剪。状态延伸到间距见独立的 08 示例。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 7`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_row_spacing::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
