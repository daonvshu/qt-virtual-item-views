# 18 表头与换序动画

显示隐藏表头、调整高度和行号宽度，列拖动时主体跟随动画。

本页直接使用默认表头，不创建自定义 header、section 或 adapter。勾选“自定义表头文字样式”，可调整对齐方式、左右 padding、字号和加粗，并观察横纵表头各自的背景色和字体颜色。参考 `Example::applyHeaderStyles()`，通过 `horizontalHeader()`、`verticalHeader()` 返回的公共接口配置：

```cpp
auto *header = view->horizontalHeader();
header->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
header->setContentPadding(QMargins(8, 2, 8, 2));
header->setBackgroundColor(QColor("#eef6ff"));
header->setForegroundColor(QColor("#334155"));
QFont font = header->font();
font.setPointSize(11);
font.setBold(true);
header->setFont(font);
```

padding 是原生内容留白之外的附加内边距，只调整文字与排序箭头，不缩小背景和分割线。字体大小不改变表头高度，使用页面已有的表头高度、行号宽度设置为较大字体提供空间。关闭样式开关后恢复默认颜色、居中、零附加 padding 和字体继承。

勾选“第 2 列表头使用模型样式”，该逻辑列会使用红色、12 pt 加粗斜体及右对齐，优先于 header 默认配置。代码通过 `setHeaderData()` 设置 `Qt::FontRole`、`Qt::ForegroundRole` 和 `Qt::TextAlignmentRole`；取消勾选传入 `QVariant()` 清除覆盖，立即恢复当前 header 默认值。拖动列后，样式跟随逻辑列。

勾选“冻结首行首列”后再调整样式，可观察冻结区域的副本同步。滚动后重新物化的 section 同样应用当前配置，不需要业务查找控件逐个修改。默认左上角跟随水平表头底色，横纵表头的文字样式互不影响。

源码相对路径：`examples/table_capabilities/18_headers/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 18`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_headers::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
