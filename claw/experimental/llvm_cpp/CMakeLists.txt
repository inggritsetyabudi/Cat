find_package(LLVM CONFIG REQUIRED)
if(NOT LLVM_VERSION_MAJOR STREQUAL "18")
    message(FATAL_ERROR "CLAW_ENABLE_LLVM_CPP_POC requires LLVM major 18 (found ${LLVM_PACKAGE_VERSION})")
endif()

add_library(claw_llvm_cpp_adapter adapter.cpp)
target_include_directories(claw_llvm_cpp_adapter PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}")
target_include_directories(claw_llvm_cpp_adapter SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
separate_arguments(CLAW_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(claw_llvm_cpp_adapter PRIVATE ${CLAW_LLVM_DEFINITIONS})
target_link_libraries(claw_llvm_cpp_adapter PUBLIC claw_frontend PRIVATE LLVM)

add_executable(claw-llvm-poc driver.cpp)
target_link_libraries(claw-llvm-poc PRIVATE claw_llvm_cpp_adapter)

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(CLAW_POC_CC NAMES cc clang REQUIRED)
    add_executable(claw-llvm-poc-adapter-tests test_adapter.cpp)
    target_link_libraries(claw-llvm-poc-adapter-tests PRIVATE claw_llvm_cpp_adapter)
    add_test(NAME llvm-cpp-poc-adapter
        COMMAND claw-llvm-poc-adapter-tests "${CMAKE_CURRENT_BINARY_DIR}/adapter-artifacts")
    add_test(NAME llvm-cpp-poc-differential
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_poc.py"
            --poc "$<TARGET_FILE:claw-llvm-poc>"
            --claw "$<TARGET_FILE:claw>"
            --cc "${CLAW_POC_CC}"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
endif()
