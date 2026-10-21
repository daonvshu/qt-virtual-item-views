# 12 滚动与定位

使用按需生成文本的 model，避免为大量逻辑行分配字符串。分别示范 Pixels / Items 滚轮模式、步长、四种 scrollTo 定位、scrollByPixels 和 qint64 setVerticalOffset。一亿行 × 36 px 可观察超过 32 位的逻辑高度。输入偏移有格式检查并回显实际偏移。平滑滚动是可选控制器集成，不属于本例的原生滚动设置。

“像素自动滚动”每 16ms 调用 `scrollByPixels(24)`，到达底部后回到开头，页面隐藏时暂停推进。默认百万行和可选一亿行都使用同一套逻辑；状态栏同时显示逻辑行数、物化数、池内数和累计创建数，可观察长时间滚动时控件数量是否稳定。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 12`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_scrolling::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
