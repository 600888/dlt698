cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" DLT698_VERSION LIMIT_COUNT 1)
    project(dlt698 VERSION ${DLT698_VERSION} LANGUAGES CXX)
    include(CTest)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
option(DLT698_BUILD_TRANSPORT "Build standalone Asio transport" ON)
option(DLT698_BUILD_EXAMPLES "Build examples" ON)
option(DLT698_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

function(dlt698_target_setup target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        # 公开头文件包含 UTF-8 中文注释，安装后的调用方也须使用相同源文件编码。
        target_compile_options(${target} PUBLIC /utf-8)
        if(DLT698_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(DLT698_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(dlt698_core
    src/common/bytes.cpp
    src/codec/data_codec.cpp
    src/protocol/link/frame.cpp
    src/protocol/apdu/get.cpp)
add_library(dlt698::core ALIAS dlt698_core)
set_target_properties(dlt698_core PROPERTIES EXPORT_NAME core VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR} CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
generate_export_header(dlt698_core EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/dlt698/export.hpp"
    EXPORT_MACRO_NAME DLT698_API STATIC_DEFINE DLT698_STATIC)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(dlt698_core PUBLIC DLT698_STATIC)
endif()
target_include_directories(dlt698_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
dlt698_target_setup(dlt698_core)

add_library(dlt698 INTERFACE)
add_library(dlt698::dlt698 ALIAS dlt698)
target_link_libraries(dlt698 INTERFACE dlt698_core)
set(dlt698_install_targets dlt698 dlt698_core)
if(DLT698_BUILD_TRANSPORT)
    add_subdirectory(transport)
    target_link_libraries(dlt698 INTERFACE dlt698_transport)
    list(APPEND dlt698_install_targets dlt698_transport)
endif()
if(DLT698_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
install(TARGETS ${dlt698_install_targets} EXPORT dlt698Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/dlt698 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "transport" EXCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/dlt698/export.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698)
set(dlt698_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/dlt698")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dlt698Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/dlt698Config.cmake" INSTALL_DESTINATION "${dlt698_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/dlt698ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT dlt698Targets NAMESPACE dlt698:: DESTINATION "${dlt698_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dlt698Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/dlt698ConfigVersion.cmake" DESTINATION "${dlt698_package_dir}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/dlt698")
