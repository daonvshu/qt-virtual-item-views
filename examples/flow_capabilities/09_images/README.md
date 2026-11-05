# 09 异步图片卡片

加载本地图片文件夹；解绑时取消订阅，避免复用后的控件显示旧图片。

源码：`examples/flow_capabilities/09_images/example.cpp`；布局：`images.ui`。

主要接口：setAdapter()、bindWidget()、unbindWidget() 与异步图片生命周期。

imagebackend.h 与 thumbnailcache.h 均在本目录。默认后备实现使用有界线程池和缓存；解绑取消请求，异步回调检查绑定身份。工程检测到可选 aster 时使用 ImageBox；单独复制时不定义 VIV_DEMO_USE_ASTER 即使用 Qt 后备实现。

运行 `flow_capabilities --example 9`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_images::createExample(parent)` 返回的 QWidget 加入布局。
