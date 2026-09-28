# SmoothScrollbar 表格试用示例

Qt 5 构建在能找到已安装的 SmoothScrollbar 时，会额外生成 `smooth_scroll_table` 示例目标；VirtualItemViews 库本身不会增加依赖。也可将这个目录作为独立 CMake 工程，用 `find_package` 链接两个已安装的库：

```powershell
cmake -S examples/smooth_scroll_table -B build-smooth-scroll-table -G Ninja `
  -DCMAKE_PREFIX_PATH="<Qt5 kit>;<VirtualItemViews 安装前缀>;<SmoothScrollbar 安装前缀>"
cmake --build build-smooth-scroll-table
```

运行 `build-smooth-scroll-table/smooth_scroll_table`（Windows 下为 `.exe`）。若使用动态库，运行前需让系统能找到两个库及 Qt5 的 DLL。滚动表格可测试纵向滚轮、Shift+滚轮横向滚动、触控板像素滚动和冻结首列；取消勾选 `Smooth wheel` 可对比表格原有滚动方式。鼠标滚轮快速连续滚动三格以上可观察惯性续滑。

此示例只接入主水平滚动组和纵向滚动条；其他水平滚动组需要单独适配。
