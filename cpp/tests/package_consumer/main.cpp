#include <dlt698/dlt698.hpp>
#ifdef HAVE_TRANSPORT
#include <dlt698/transport/tcp.hpp>
#endif
int main() {
    // 仅通过安装后的公开头文件和库调用 API，验证导出目标不依赖源码目录。
    const auto data = dlt698::codec::encode_data(dlt698::model::UInt16{2413});
    if (!data || dlt698::to_hex(data.value()) != "12 09 6D") return 1;
#ifdef HAVE_TRANSPORT
    dlt698::transport::IoRuntime runtime;
    runtime.stop();
#endif
    return 0;
}
