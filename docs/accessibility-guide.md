# 可访问性与运行诊断

## 可访问性

在创建视图前注册辅助功能工厂：

```cpp
#include <virtualitemviews/accessibility.h>

viv::installAccessibilityFactory();
```

工厂按需给可见列表项、树节点、表格行和单元格提供接口；文本、状态与几何取自当前模型和视图。表格还暴露行列及单元格语义，支持 current、选择及 press/focus/scroll 等动作。虚拟节点暂不提供文本和可编辑文本接口；行内真实 QWidget 自身的编辑接口仍由该控件提供。

## 查看虚拟化效果

```cpp
#include <QDebug>

const viv::VirtualViewStats stats = view.stats();
qDebug() << "logical" << stats.logicalItems
         << "visible widgets" << stats.materializedItems
         << "pooled" << stats.pooledWidgets
         << "pinned" << stats.pinnedWidgets;
view.setLifecycleLoggingEnabled(true);
const QStringList events = view.lifecycleLog();
```

`stats()` 还包含创建/绑定/回收计数。生命周期日志有界且默认关闭；定位控件数量持续增长时先检查 pin 和 adapter 的解绑逻辑。

固定高度或仅有少量尺寸例外的模型无需为每个逻辑行保存控件或完整尺寸状态。`logicalItems` 很大而 `materializedItems` 仍接近视口容量时，说明虚拟化窗口按预期工作。
