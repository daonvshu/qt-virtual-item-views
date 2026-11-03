# 18 行内事件与几何查询

行内事件使用每次 bind 更新、unbind 清空的 QPersistentModelIndex；滚出视口后数据和选择仍有效，控件可能已回收。查询区分父节点、兄弟行号、可见行号和单元格几何。

入口：[example.cpp](example.cpp)。主要接口：`cellIndexForWidget`、`cellWidget`、`cellRect`、`indexAt`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 18`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
