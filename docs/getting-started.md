# 入门：模型、Adapter 与视图

VirtualItemViews 使用 Qt 的 `QAbstractItemModel` 提供数据，用 adapter 创建和复用真实的 `QWidget`。视图只物化可见区、overscan 和 pinned 的项；模型的插入、删除、移动、`dataChanged` 和重置会触发更新，无需手动 `reload()`。

## 最小列表

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

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(400, 32); }
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

需要 C++17、CMake 3.16+、Qt 6.2+ 或 Qt 5.15+（Core、Gui、Widgets）。仓库默认构建静态库、示例、测试和基准程序。

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

下一步：[列表与行高](list.md)、[表格与单元格](table.md)、[树视图](tree.md)。
