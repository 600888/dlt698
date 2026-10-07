add_library(dlt698_app ../src/app/server.cpp ../src/app/client.cpp)
add_library(dlt698::app ALIAS dlt698_app)
set_target_properties(dlt698_app PROPERTIES EXPORT_NAME app VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR} CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
generate_export_header(dlt698_app
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/../include/dlt698/app_export.hpp"
    EXPORT_MACRO_NAME DLT698_APP_API STATIC_DEFINE DLT698_APP_STATIC)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(dlt698_app PUBLIC DLT698_APP_STATIC)
endif()
target_link_libraries(dlt698_app PUBLIC dlt698_service dlt698_transport PRIVATE Threads::Threads)
dlt698_target_setup(dlt698_app)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/../include/dlt698/app_export.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698)
