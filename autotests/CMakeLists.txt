include(ECMMarkAsTest)
add_subdirectory(migrateconfig)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
    add_executable(softwarebrightnesstest softwarebrightnesstest.cpp
        ../daemon/controllers/softwarebrightness.cpp
        ../daemon/controllers/displaybrightness.cpp)
    target_include_directories(softwarebrightnesstest PRIVATE ../daemon ${CMAKE_BINARY_DIR}/daemon)
    target_link_libraries(softwarebrightnesstest Qt::Test Qt::DBus KF6::I18n)
    add_test(NAME powerdevil-softwarebrightness COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:softwarebrightnesstest>)
endif()
