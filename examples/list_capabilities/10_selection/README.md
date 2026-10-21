# 10 选择与当前项

两个 view 使用同一个 model 和 QItemSelectionModel，各自拥有自己的 adapter。下拉框切换四种选择手势模式。程序选择使用 QItemSelection + ClearAndSelect；再通过 selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate) 更新当前项，保留已选范围。因此四种模式下点击按钮都会选中第 2～5 行，当前项为第 5 行。模式约束鼠标 / 键盘选择手势，直接调用 selectionModel 仍可程序选择多项。

行控件使用普通透明 `QLabel`，`TextAdapter` 只绑定和清理文本。hover、selected 背景及过渡动画由框架绘制，圆角通过 `setVisualStateCornerRadius(8)` 配置，颜色使用对应的背景颜色接口。共享选择模型同步选中项和当前项，hover 则由每个 view 独立维护，无需自绘或重写 `visualStateChanged()`。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 10`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_selection::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
