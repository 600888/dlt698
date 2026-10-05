dlt698_add_test(transport
    SOURCES memory_test.cpp serial_link_test.cpp tcp_test.cpp
    LIBS dlt698_transport dlt698_service)