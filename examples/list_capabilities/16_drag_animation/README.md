# 16 行移动动画拖动

本例只演示行移动动画：拖动时其他行滑动让位，放下后才修改 model；取消、离开或拒绝落点后恢复原位置。插入指示器的颜色、线宽和线型单独见 [15 指示器拖动](../15_drag_drop/README.md)。两个目录各自包含完整代码，不互相引用实现。

## 阅读入口

页面第一行切换 Automatic / Manual 和默认 Move / Copy；第二行调整行移动时长和曲线，默认 180ms / OutCubic，0ms 表示立即让位。这些参数与 hover 动画相互独立：

```cpp
view.setDragFeedbackMode(viv::VirtualListView::DragFeedbackMode::RowMoveAnimation);
view.setDragReorderAnimationDuration(180);
view.setDragReorderAnimationEasingCurve(QEasingCurve::OutCubic);
```

动画适用于本列表当前 `rootIndex()` 下滚动区域内的单行 Move。复制、多行、跨视图和跨冻结行区域沿用默认指示器；本例可通过 Ctrl 或“默认复制”观察复制时的表现。Automatic 和 Manual 共用相同的动画与落点处理，动画不会提前改变 model 顺序。

`QStandardItemModel` 负责 MIME 和新行插入，item flags 允许起拖，根项允许接收插入。`setMoveRemovesSourceRows(true)` 让 view 在 Move 成功后删除源行，并让新条目继承原有选择和当前项；自定义 model 已通过 `moveRows()` 移动原行时，必须关闭该选项。复制、取消和源行删除失败不会转移选择。

Automatic 使用框架的拖动距离判断。Manual 由本地 `DragHandle` 判断手势，adapter 在事件发生时通过 `indexForWidget()` 获取复用后的正确索引，再调用 `startDrag(index, options)`，传入原始全局按下位置。两种模式都使用默认行控件预览；临时 pin、拖动期间 hover 暂停、自动滚动和结束清理由框架处理。行背景通过框架的颜色和圆角接口配置。

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 16`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_drag_animation::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
