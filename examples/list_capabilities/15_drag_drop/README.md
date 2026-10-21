# 15 拖放

model 使用 Qt 标准 QStandardItemModel 的 MIME 和插入实现；item flags 允许起拖，根项允许接收插入。Move 成功后由 setMoveRemovesSourceRows(true) 清理源行；自定义 model 如果已执行 moveRows，必须关闭该选项。Automatic 使用框架距离判断和默认预览。Manual 的本地 DragHandle 只判断手势，adapter 在事件发生时查当前 index，并通过 DragOptions 传入全局按下位置；两种模式均使用默认行控件预览。源状态、指示线、自动滚动和拖动期间 hover 暂停均由框架维护。行内圆角状态绘制也完整保存在本目录。

Automatic 和 Manual 成功移动选中项后，新条目会在源行删除后继承选择；原当前项也会跟随移动。框架记录实际插入的新行并使用持续索引跟踪，向上或向下移动均不依赖删除后的行号。复制、取消和源行删除失败不会转移选择。该行为适用于同级源行按顺序插入为单个新行块的落点；已有单元格覆盖、混合父节点或无法确定对应关系的自定义落点仍由业务管理选择。

## 阅读入口

页面第二行可调整指示器颜色、线宽和线型，对应 `setDropIndicatorColor()`、`setDropIndicatorWidth()`、`setDropIndicatorPenStyle()`。默认跟随调色板高亮色、2 个逻辑像素、实线；无效 `QColor()` 恢复默认颜色，线宽为 0 或线型为 `Qt::NoPen` 时隐藏指示器。这里只改变表现，落点权限和自动滚动仍由框架与 model 处理。

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 15`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_drag_drop::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
