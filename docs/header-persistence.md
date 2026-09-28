# 表格列布局持久化

`VirtualTableView::saveHeaderState()` 返回 `QByteArray`，`restoreHeaderState()` 用该数据恢复表格状态。可直接把字节数组存入 `QSettings` 的 INI 文件，无需自行序列化列号。保存内容包括视觉列顺序、列宽、隐藏状态、排序指示器、水平滚动偏移以及冻结的行列；它不会保存模型数据，也不会替模型执行排序。

## 保存到 INI

下面代码放在创建 `QApplication app`、模型 `model` 和表格 `table` 的同一作用域中。`settings` 应存活到事件循环退出；表格也应在退出回调执行时仍然存在。

```cpp
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

QCoreApplication::setOrganizationName(QStringLiteral("Acme"));
QCoreApplication::setApplicationName(QStringLiteral("Orders"));
table.setModel(&model);
table.setColumnDragEnabled(true); // 允许用户拖动调整视觉列顺序

const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
if (configDir.isEmpty() || !QDir().mkpath(configDir))
    return 1;
QSettings settings(QDir(configDir).filePath(QStringLiteral("table.ini")),
                   QSettings::IniFormat);

const QString stateKey = QStringLiteral("orders/v1/headerState");
const QByteArray saved = settings.value(stateKey).toByteArray();
if (!saved.isEmpty() && !table.restoreHeaderState(saved))
    settings.remove(stateKey);

QObject::connect(&app, &QCoreApplication::aboutToQuit, &table, [&]() {
    settings.setValue(stateKey, table.saveHeaderState());
    settings.sync();
});
```

`setModel()` 要在恢复之前调用，因为恢复会校验列数。用户拖动列、修改列宽或隐藏列后，关闭并重新启动程序即可看到恢复效果。实际 INI 路径可通过 `settings.fileName()` 查看；`QStandardPaths::AppConfigLocation` 由系统、组织名和应用名决定。若只希望记住本次会话内的状态，无须 `QSettings`，直接保存 `QByteArray` 即可。

## 列结构变化时

状态按逻辑列号保存，不按列标题匹配。模型列数改变时，`restoreHeaderState()` 会返回 `false`；若列数不变但列号代表的业务字段变了，旧状态可能恢复到错误的列。改变列结构时更新键名中的版本（例如从 `orders/v1/headerState` 改为 `orders/v2/headerState`），或主动删除旧键。恢复失败时保留表格当前布局，并可移除无效配置。

如果用户需要“恢复默认布局”，在读 INI 前先记录 `const QByteArray defaultState = table.saveHeaderState();`，点击重置时调用 `table.restoreHeaderState(defaultState)`；正常退出时再保存当前状态即可。
