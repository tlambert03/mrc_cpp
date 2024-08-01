# Add Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP true
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Add test executable
include_directories(${CMAKE_SOURCE_DIR}/src)
add_executable(test_dvfile test_dvfile.cpp)
target_link_libraries(test_dvfile gtest gtest_main)

# Copy the test data file to the build directory
add_custom_command(TARGET test_dvfile POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy
                   ${CMAKE_CURRENT_SOURCE_DIR}/example.dv
                   $<TARGET_FILE_DIR:test_dvfile>/example.dv)

# Add tests
add_test(NAME DVFileTest COMMAND test_dvfile)
set_tests_properties(DVFileTest PROPERTIES WORKING_DIRECTORY $<TARGET_FILE_DIR:test_dvfile>)