# 13 虚拟化与复用

`ReusedRow::instanceId` 标识控件实例，bind 展示当前数据，可直接观察同一个实例绑定不同条目。上下 overscan 分开控制。visibleItemRange 不含 overscan；stats 的 materializedItems 包含已物化项。此例不创建一万份控件，也不手动维护复用池。

第二行演示 `recycler()` 的空闲池配置：`setMaxPoolSize()` 按 `WidgetType` 限制空闲控件数，0 表示不保留新回收的控件，-1 表示不限量；不限制可见或 pin 的控件。`trim()` 按各类型上限裁剪已有缓存，`clear()` 清空所有空闲缓存，不解绑正在使用的行。本例只有默认类型，多类型列表可为各类型分别配置。

先把上下预加载调大，再调小，就能产生空闲缓存。降低池上限后点击“按上限裁剪池”，或点击“清空空闲池”，观察池内数、累计销毁数及后续滚动时的创建数与复用命中数。销毁统计以实际销毁为准，延迟删除可能稍后才计入。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 13`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_virtualization::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
