# 17 屏外保留

按 index 保留能直接物化屏外项；按 widget 保留先定位、完成待处理布局并取出 widget。解除使用 unpinWidget 或 setItemPinned(false)。本例用 QSet<QPersistentModelIndex> 记录显式请求，不与焦点 / 弹窗保留混淆。setMaxPinnedItems 是诊断软上限，0 关闭检查，不会强制释放。编辑器在 editingFinished 时提交数据；异步操作应由业务在完成、取消或失败时配对解除保留。

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
    QWidget *page = list_pinning::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
