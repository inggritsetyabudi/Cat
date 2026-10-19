# Compile the exact production runtime once as C11, then archive its one object.
enable_language(C)
if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "Runtime requires native x64 (cross compilation is rejected)")
endif()
set(CLAW_RUNTIME_SOURCE "${PROJECT_SOURCE_DIR}/runtime/native_runtime.c")
if(NOT EXISTS "${CLAW_RUNTIME_SOURCE}")
    message(FATAL_ERROR "Missing unmodified native runtime source: ${CLAW_RUNTIME_SOURCE}")
endif()
execute_process(COMMAND "${CMAKE_C_COMPILER}" --version RESULT_VARIABLE _cc_version_status
    OUTPUT_VARIABLE CLAW_RUNTIME_COMPILER_VERSION ERROR_VARIABLE _cc_version_error OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${CMAKE_C_COMPILER}" -dumpmachine RESULT_VARIABLE _cc_target_status
    OUTPUT_VARIABLE CLAW_RUNTIME_COMPILER_TARGET ERROR_VARIABLE _cc_target_error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _cc_version_status EQUAL 0 OR NOT _cc_target_status EQUAL 0)
    message(FATAL_ERROR "Runtime compiler probes failed: --version=${_cc_version_status}, -dumpmachine=${_cc_target_status}")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 13.0 OR
       NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 14.0 OR
       NOT CLAW_RUNTIME_COMPILER_TARGET MATCHES "^x86_64(-unknown)?-linux-gnu$")
        message(FATAL_ERROR "Linux runtime requires native GCC 13 x86_64-linux-gnu (ELF64)")
    endif()
    set(CLAW_RUNTIME_PROFILE "linux-x86_64-elf")
    set(CLAW_RUNTIME_OBJECT_FORMAT "ELF64")
elseif(WIN32 AND MINGW)
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_DEFINITIONS "")
    check_c_source_compiles("#include <_mingw.h>\n#if !defined(__MINGW64__) || !defined(_UCRT) || !defined(_WIN32)\n#error UCRT64 MinGW required\n#endif\n_Static_assert(sizeof(void*) == 8, \"x64 required\");\nint main(void) { return 0; }" CLAW_RUNTIME_UCRT_X64)
    if(NOT CLAW_RUNTIME_UCRT_X64 OR NOT CLAW_RUNTIME_COMPILER_TARGET MATCHES "^x86_64-w64-(mingw32|windows-gnu)$")
        message(FATAL_ERROR "Windows runtime requires native x64 MinGW UCRT (not MSVC/MSVCRT/cross)")
    endif()
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_C_COMPILER_VERSION VERSION_EQUAL 23.1.3)
        message(FATAL_ERROR "Windows Clang runtime requires exact Clang 23.1.3")
    elseif(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "Windows runtime compiler must be GNU or Clang GNU-target")
    endif()
    set(CLAW_RUNTIME_PROFILE "windows-x86_64-ucrt-coff")
    set(CLAW_RUNTIME_OBJECT_FORMAT "COFF64")
else()
    message(FATAL_ERROR "Runtime supports only native x64 Linux ELF or Windows UCRT COFF")
endif()
set(CLAW_RUNTIME_FLAGS -std=gnu11 -ffunction-sections -fdata-sections -fno-lto)
add_library(claw_llvm_cpp_poc_runtime_objects OBJECT "${CLAW_RUNTIME_SOURCE}")
set_target_properties(claw_llvm_cpp_poc_runtime_objects PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
target_compile_options(claw_llvm_cpp_poc_runtime_objects PRIVATE ${CLAW_RUNTIME_FLAGS})
add_library(claw_llvm_cpp_poc_runtime STATIC $<TARGET_OBJECTS:claw_llvm_cpp_poc_runtime_objects>)
set(CLAW_RUNTIME_PACKAGE "${CMAKE_CURRENT_BINARY_DIR}/artifacts/package")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_runtime_inspection_args)
if(WIN32)
    find_program(CLAW_RUNTIME_NM NAMES llvm-nm REQUIRED)
    find_program(CLAW_RUNTIME_AR NAMES llvm-ar REQUIRED)
    find_program(CLAW_RUNTIME_READOBJ NAMES llvm-readobj REQUIRED)
    list(APPEND _runtime_inspection_args --readobj "${CLAW_RUNTIME_READOBJ}")
else()
    find_program(CLAW_RUNTIME_NM NAMES nm REQUIRED)
    find_program(CLAW_RUNTIME_AR NAMES ar REQUIRED)
endif()
add_custom_command(TARGET claw_llvm_cpp_poc_runtime POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_package.py"
        --source "${CLAW_RUNTIME_SOURCE}" --object "$<TARGET_OBJECTS:claw_llvm_cpp_poc_runtime_objects>"
        --archive "$<TARGET_FILE:claw_llvm_cpp_poc_runtime>" --package "${CLAW_RUNTIME_PACKAGE}"
        --compiler "${CMAKE_C_COMPILER}" --profile "${CLAW_RUNTIME_PROFILE}"
        --object-format "${CLAW_RUNTIME_OBJECT_FORMAT}" --nm "${CLAW_RUNTIME_NM}" --ar "${CLAW_RUNTIME_AR}"
        ${_runtime_inspection_args}
        --license "${PROJECT_SOURCE_DIR}/../LICENSE"
        --flag=-std=gnu11 --flag=-ffunction-sections --flag=-fdata-sections --flag=-fno-lto
        "--flag=${CMAKE_C_FLAGS}" "--flag=${CMAKE_C_FLAGS_RELEASE}"
    VERBATIM)
if(BUILD_TESTING)
    add_test(NAME llvm-cpp-runtime-configure-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_config.py"
            --cmake "${CMAKE_COMMAND}" --source-dir "${PROJECT_SOURCE_DIR}"
            --cxx-compiler "${CMAKE_CXX_COMPILER}" --generator "${CMAKE_GENERATOR}")
    set(_runtime_profile_args --manifest "${CLAW_RUNTIME_PACKAGE}/runtime-manifest.json")
    if(WIN32)
        get_filename_component(_runtime_prefix "${CLAW_LLD_LIBDIR}" DIRECTORY)
        list(APPEND _runtime_profile_args --ucrt-prefix "${_runtime_prefix}" --readobj "${CLAW_RUNTIME_READOBJ}")
    endif()
    add_test(NAME llvm-cpp-poc-runtime
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_runtime.py"
            --emitter "$<TARGET_FILE:claw-llvm-poc>" --production "$<TARGET_FILE:claw>"
            --runtime "$<TARGET_FILE:claw_llvm_cpp_poc_runtime>" --external "${CLAW_LLD_EXECUTABLE}"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts" --profile "${CLAW_LLVM_POC_PLATFORM}"
            ${_runtime_profile_args})
    set_tests_properties(llvm-cpp-poc-runtime PROPERTIES DEPENDS llvm-cpp-poc-differential TIMEOUT 180)
    # Existing native wrapping semantics only: O0/O2 are optimization profiles,
    # not a newly invented Cat debug-safety profile. Keep the old gates intact.
    add_test(NAME llvm-cpp-poc-arithmetic
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_arithmetic.py"
            --emitter "$<TARGET_FILE:claw-llvm-poc>" --production "$<TARGET_FILE:claw>"
            --runtime "$<TARGET_FILE:claw_llvm_cpp_poc_runtime>" --external "${CLAW_LLD_EXECUTABLE}"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts/arithmetic" --profile "${CLAW_LLVM_POC_PLATFORM}"
            ${_runtime_profile_args})
    set_tests_properties(llvm-cpp-poc-arithmetic PROPERTIES DEPENDS llvm-cpp-poc-runtime TIMEOUT 240)
    # Production ABI/cleanup evidence only: its calls omit --strings and must
    # continue to reject every String input at the default adapter boundary.
    find_program(CLAW_ABI_ORACLE_CLANG NAMES clang REQUIRED)
    # Native UCRT Python cannot resolve MSYS /usr/bin through CreateProcess.
    find_program(CLAW_ABI_ORACLE_GIT NAMES git
        HINTS "$ENV{ProgramFiles}/Git/cmd" "$ENV{ProgramW6432}/Git/cmd"
              "C:/Program Files/Git/cmd" "C:/Program Files/Git/bin" REQUIRED)
    execute_process(COMMAND "${CLAW_ABI_ORACLE_GIT}" -C "${PROJECT_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE _abi_oracle_source_sha OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _abi_oracle_git_status)
    if(NOT _abi_oracle_git_status EQUAL 0 OR NOT _abi_oracle_source_sha MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "String ABI evidence requires a verifiable Git source revision")
    endif()
    add_test(NAME llvm-cpp-production-string-abi-oracle
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../abi_oracle/run_oracle.py"
            --production "$<TARGET_FILE:claw>" --emitter "$<TARGET_FILE:claw-llvm-poc>"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts/abi-oracle"
            --cc "${CLAW_ABI_ORACLE_CLANG}" --profile "${CLAW_LLVM_POC_PLATFORM}"
            --git "${CLAW_ABI_ORACLE_GIT}" --expected-sha "${_abi_oracle_source_sha}")
    set_tests_properties(llvm-cpp-production-string-abi-oracle PROPERTIES
        DEPENDS llvm-cpp-poc-arithmetic TIMEOUT 600)
    add_test(NAME llvm-cpp-poc-strings
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_strings.py"
            --emitter "$<TARGET_FILE:claw-llvm-poc>" --production "$<TARGET_FILE:claw>"
            --runtime "$<TARGET_FILE:claw_llvm_cpp_poc_runtime>" --external "${CLAW_LLD_EXECUTABLE}"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts/strings" --profile "${CLAW_LLVM_POC_PLATFORM}"
            --cc "${CLAW_ABI_ORACLE_CLANG}" --ar "${CLAW_RUNTIME_AR}"
            ${_runtime_profile_args})
    set_tests_properties(llvm-cpp-poc-strings PROPERTIES
        DEPENDS llvm-cpp-production-string-abi-oracle TIMEOUT 600)
endif()
