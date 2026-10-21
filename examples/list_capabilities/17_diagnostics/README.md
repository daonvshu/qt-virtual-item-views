# 17 运行诊断

stats 是即时快照，create / bind / recycle 是累计计数，不能与当前物化数混为一谈。定时器仅刷新统计文本，日志通过按钮取得快照，便于阅读。生命周期记录可开关和清空；kLifecycleLogCapacity 限制框架日志条数。修改当前行可观察重绑，滚动可观察回收和复用。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 17`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_diagnostics::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
