# 07 展开收起与过渡动画

展开：当前节点可展开、收起、切换或递归展开。方向键右/左和 * 也可操作。

源码相对路径：`examples/tree_capabilities/07_expansion/example.cpp`。

组件默认开启真实布局动画，此页可用复选框关闭。`expandRecursively()` 展开当前节点及其后代，`collapseAll()` 即时收起全部分支。`expanded` 在开始展开时发送；收起时 `isExpanded()` 立即返回 `false`，退出中的子节点及可见行映射保留到动画结束，随后发送 `collapsed`。时长为 0 或设置冻结行时即时完成；已展开节点的递归展开也即时完成。动画使用真实控件及原生矩形裁剪，不使用截图覆盖层。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

“子节点淡入淡出”默认勾选，与框架默认启用一致；调用 `setExpansionFadeEnabled(false)` 可单独关闭渐变，保留高度动画。仅当前分支的子节点渐显或渐隐，父节点和后续分组保持不透明；透明度与高度共享时长和 OutCubic 曲线，反向操作从当前值继续。关闭“动画”或将时长设为 0 时立即完成。`expansionFadeEnabled()` 可查询配置。

渐变由框架临时容器的 `QGraphicsOpacityEffect` 合成实时控件，不替换业务控件自身的 effect。Qt 可能使用离屏缓冲，因此开启渐变会增加绘制成本；没有静态截图覆盖层。

运行 `tree_capabilities --example 7` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_expansion::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
