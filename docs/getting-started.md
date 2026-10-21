# 入门：模型、Adapter 与视图

VirtualItemViews 使用 Qt 的 `QAbstractItemModel` 提供数据，用 adapter 创建和复用真实的 `QWidget`。视图只物化可见区、overscan 和 pinned 的项；模型的插入、删除、移动、`dataChanged` 和重置会触发更新，无需手动 `reload()`。

## 最小列表

完整可运行的列表接入代码见 [基础列表示例](../examples/list_capabilities/01_basic/example.cpp)，model、adapter 和页面装配都在同一个文件中。更多列表能力见 [17 项独立示例](../examples/list_capabilities/README.md)；其他视图从 [examples 导航](../examples/README.md)查找。

以下是完整的 `main.cpp`。需要 `QApplication`，并让模型和 adapter 比视图活得更久。

```cpp
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>

#include <QApplication>
#include <QLabel>
#include <QStringListModel>

class RowAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(400, 32); }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStringListModel model({QStringLiteral("第一行"), QStringLiteral("第二行")});
    RowAdapter adapter;
    viv::VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(32);
    view.setModel(&model);
    view.show();
    return app.exec();
}
```

`createWidget()` 只负责创建。`bindWidget()` 必须在每次复用时覆盖旧内容；若控件持有定时器、动画、异步请求或订阅，在 `unbindWidget()` 中解除与旧 `QModelIndex` 的关联。adapter 默认由调用方持有；只有显式传入 `takeOwnership = true` 才交给视图管理。

## 构建与链接

需要 CMake 3.16+、Qt 6.2+ 或 Qt 5.12.3+（Core、Gui、Widgets）。Qt5 构建使用 C++14，Qt6 构建使用 C++17，链接导出的 CMake target 会自动传递相应标准要求。仓库默认构建静态库、示例、测试和基准程序。

源码通过 `QT_VERSION` / `QT_VERSION_CHECK` 选择兼容写法：旧 Qt 使用可用的状态流版本、`QString::SkipEmptyParts` 和自定义 `QRunnable`，新 Qt 保留对应的新 API。安装包也会按构建时选择的 Qt 主版本传递最低版本要求。

Qt5 配合旧版交叉编译工具链时，不需要 `<optional>` 等 C++17 标准库头文件。GCC 7 之前会跳过使用 C++17 测试助手的 `tst_treerowspacing` 目标，并在配置输出中说明；库、示例和其他测试仍按 C++14 构建。

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build
cmake --install build --prefix /path/to/install
```

需要动态库时传 `-DVIRTUALITEMVIEWS_BUILD_SHARED=ON`。示例、测试和基准程序可分别通过 `VIRTUALITEMVIEWS_BUILD_EXAMPLES`、`VIRTUALITEMVIEWS_BUILD_TESTS` 和 `VIRTUALITEMVIEWS_BUILD_BENCHMARKS` 关闭。安装后，在消费端的 `CMAKE_PREFIX_PATH` 中提供安装前缀和 Qt 路径：

```cmake
find_package(VirtualItemViews REQUIRED)
target_link_libraries(app PRIVATE VirtualItemViews::VirtualItemViews)
```

下一步：[列表与行高](list.md)、[表格与单元格](table.md)、[流式表格](flow-table.md)、[瀑布流](masonry.md)、[树视图](tree.md)。
