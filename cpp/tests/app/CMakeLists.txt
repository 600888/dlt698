dlt698_add_test(app SOURCES server_test.cpp client_test.cpp LIBS dlt698_app)
set_tests_properties(app PROPERTIES TIMEOUT 60 LABELS "network")
