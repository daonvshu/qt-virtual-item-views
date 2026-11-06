# 异步图片卡片

先异步扫描图片清单取得原图尺寸，再按比例安排瀑布流；缩略图只在卡片物化后加载。解绑取消旧回调，完成解码不反复改变卡片高度。

入口为 [example.cpp](example.cpp)，页面为 [images.ui](images.ui)，卡片与 adapter 为 [cards.h](cards.h)。本目录包含独立的图片清单扫描器、缩略图缓存和后端，不依赖其他章节。需要完整复制本目录。

只链接框架和 Qt Widgets 即可使用 Qt 缩略图后端。主示例通过 `viv_demo_images` 自动选择与当前 Qt 匹配的可选 aster；独立工程需要主动链接 `aster::gui` 并定义 `VIV_DEMO_USE_ASTER=1` 才启用它。
