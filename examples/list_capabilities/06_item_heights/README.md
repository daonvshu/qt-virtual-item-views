# 06 行高

`DetailRow::sizeHint()` 按绑定数据返回高度。下拉框分别演示固定高度、Uniform + 高度 0 + 自动测量、Variable + 估计高度 + 自动测量。点击行仅改 model 的 ExpandedRole。动态模式滚动到中段后切换首行详情，可观察上方高度变化时的锚点补偿。统一高度模式下，详情可能被固定行高裁剪，这是模式本身的效果。

`setUniformItemHeight()` 会同时切换到 `Uniform` 模式，因此动态模式只设置 `Variable`、估计高度和自动测量，不调用统一高度接口。动态模式中，收起行的高度为 36，展开详情后为 112，单位为逻辑像素。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 6`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_item_heights::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
