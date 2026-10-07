set(DLT698_ASIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third/asio/include" CACHE PATH "Standalone Asio include directory")
if(NOT EXISTS "${DLT698_ASIO_INCLUDE_DIR}/asio.hpp")
    message(FATAL_ERROR "Standalone Asio not found. Set DLT698_ASIO_INCLUDE_DIR or disable DLT698_BUILD_TRANSPORT.")
endif()
add_library(dlt698_transport ../src/transport/tcp.cpp ../src/transport/serial.cpp)
add_library(dlt698::transport ALIAS dlt698_transport)
set_target_properties(dlt698_transport PROPERTIES EXPORT_NAME transport VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR} CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
generate_export_header(dlt698_transport EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/dlt698/transport_export.hpp"
    EXPORT_MACRO_NAME DLT698_TRANSPORT_API STATIC_DEFINE DLT698_TRANSPORT_STATIC)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(dlt698_transport PUBLIC DLT698_TRANSPORT_STATIC)
endif()
target_include_directories(dlt698_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(dlt698_transport SYSTEM PRIVATE "${DLT698_ASIO_INCLUDE_DIR}")
# 运行时允许跨线程投递，所有翻译单元必须使用同一套有线程 Asio 类型。
# MinGW 的 _MT 可由标准库头文件间接定义；依赖 include 顺序会混用 TLS 键和普通指针。
target_compile_definitions(dlt698_transport PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_THREADS)
target_link_libraries(dlt698_transport PUBLIC dlt698_core PRIVATE Threads::Threads)
if(WIN32)
    target_compile_definitions(dlt698_transport PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(dlt698_transport PRIVATE ws2_32 mswsock)
endif()
dlt698_target_setup(dlt698_transport)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/dlt698/transport"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/dlt698/transport_export.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../third/asio/LICENSE_1_0.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dlt698 RENAME ASIO_LICENSE_1_0.txt)
