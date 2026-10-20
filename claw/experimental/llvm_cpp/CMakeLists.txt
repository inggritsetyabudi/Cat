# The selective SDK intentionally has no LLVMConfig.cmake.  Discover it only
# through the explicit llvm-config contract, and never silently fall back.
if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "The LLVM C++ PoC requires native x86_64; cross compilation is not validated")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CLAW_LLVM_POC_PLATFORM "linux-x86_64-elf")
elseif(WIN32 AND MINGW)
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("#include <_mingw.h>\n#if !defined(__x86_64__) || !defined(__MINGW64__) || !defined(_UCRT)\n#error Native x64 UCRT compiler required\n#endif\nint main() { return 0; }" CLAW_LLVM_POC_COMPILER_IS_UCRT_X64)
    if(NOT CLAW_LLVM_POC_COMPILER_IS_UCRT_X64)
        message(FATAL_ERROR "Windows PoC requires a native x64 MinGW UCRT compiler; MSVCRT and MSVC are not supported")
    endif()
    set(CLAW_LLVM_POC_PLATFORM "windows-x86_64-ucrt-coff")
else()
    message(FATAL_ERROR "PoC platforms are native Linux x86_64 ELF and Windows x64 MinGW UCRT COFF only")
endif()

set(CLAW_LLVM_CONFIG "" CACHE FILEPATH
    "Path to the LLVM 23.1.3 llvm-config executable")
if(NOT CLAW_LLVM_CONFIG)
    find_program(CLAW_LLVM_CONFIG_DEFAULT NAMES llvm-config-23 llvm-config)
    set(CLAW_LLVM_CONFIG "${CLAW_LLVM_CONFIG_DEFAULT}" CACHE FILEPATH
        "Path to the LLVM 23.1.3 llvm-config executable" FORCE)
endif()
if(NOT CLAW_LLVM_CONFIG OR NOT EXISTS "${CLAW_LLVM_CONFIG}")
    message(FATAL_ERROR
        "CLAW_ENABLE_LLVM_CPP_POC requires an executable LLVM 23.1.3 llvm-config; "
        "set CLAW_LLVM_CONFIG=/path/to/llvm23-sdk/bin/llvm-config")
endif()

function(claw_llvm_config result)
    execute_process(
        COMMAND "${CLAW_LLVM_CONFIG}" ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "llvm-config ${ARGN} failed (${_result}): ${_error}")
    endif()
    set(${result} "${_output}" PARENT_SCOPE)
endfunction()

claw_llvm_config(CLAW_LLVM_VERSION --version)
if(NOT CLAW_LLVM_VERSION STREQUAL "23.1.3")
    message(FATAL_ERROR
        "CLAW_ENABLE_LLVM_CPP_POC requires exact LLVM 23.1.3; llvm-config reported '${CLAW_LLVM_VERSION}'")
endif()
claw_llvm_config(CLAW_LLVM_INCLUDEDIR --includedir)
claw_llvm_config(CLAW_LLVM_HOST_TARGET --host-target)
claw_llvm_config(CLAW_LLVM_CPPFLAGS --cppflags)
claw_llvm_config(CLAW_LLVM_HAS_RTTI --has-rtti)
claw_llvm_config(CLAW_LLVM_LIBFILES --link-static --libfiles core codegen mc target x86codegen x86desc x86info)
claw_llvm_config(CLAW_LLVM_SYSTEM_LIBS --link-static --system-libs)

if(WIN32)
    if(NOT CLAW_LLVM_HOST_TARGET MATCHES "^(x86_64.*windows.*gnu|x86_64-w64-mingw32)$")
        message(FATAL_ERROR "LLVM SDK host target '${CLAW_LLVM_HOST_TARGET}' is not Windows x86_64/UCRT")
    endif()
elseif(UNIX)
    if(NOT CLAW_LLVM_HOST_TARGET MATCHES "^x86_64.*linux.*gnu")
        message(FATAL_ERROR "LLVM SDK host target '${CLAW_LLVM_HOST_TARGET}' is not GNU/Linux x86_64")
    endif()
endif()

if(NOT IS_DIRECTORY "${CLAW_LLVM_INCLUDEDIR}" OR
   NOT EXISTS "${CLAW_LLVM_INCLUDEDIR}/llvm/Config/llvm-config.h")
    message(FATAL_ERROR "LLVM 23.1.3 include directory is missing llvm/Config/llvm-config.h: ${CLAW_LLVM_INCLUDEDIR}")
endif()
file(READ "${CLAW_LLVM_INCLUDEDIR}/llvm/Config/llvm-config.h" CLAW_LLVM_CONFIG_HEADER)
if(NOT CLAW_LLVM_CONFIG_HEADER MATCHES "#define LLVM_VERSION_STRING \"23\\.1\\.3\"")
    message(FATAL_ERROR "LLVM header provenance is not exact version 23.1.3: ${CLAW_LLVM_INCLUDEDIR}")
endif()

separate_arguments(CLAW_LLVM_LIBFILES_LIST NATIVE_COMMAND "${CLAW_LLVM_LIBFILES}")
if(NOT CLAW_LLVM_LIBFILES_LIST)
    message(FATAL_ERROR "llvm-config returned no explicit static LLVM libraries")
endif()
foreach(CLAW_LLVM_LIB IN LISTS CLAW_LLVM_LIBFILES_LIST)
    if(NOT IS_ABSOLUTE "${CLAW_LLVM_LIB}" OR NOT EXISTS "${CLAW_LLVM_LIB}")
        message(FATAL_ERROR "llvm-config returned a missing/non-absolute static library: ${CLAW_LLVM_LIB}")
    endif()
    if(NOT CLAW_LLVM_LIB MATCHES "libLLVM[^/]*\\.a$")
        message(FATAL_ERROR "LLVM link closure contains a non-LLVM or non-static library: ${CLAW_LLVM_LIB}")
    endif()
    get_filename_component(CLAW_LLVM_LIB_BASENAME "${CLAW_LLVM_LIB}" NAME)
    if(CLAW_LLVM_LIB_BASENAME STREQUAL "libLLVM.a" OR CLAW_LLVM_LIB_BASENAME MATCHES "\\.dll\\.a$")
        message(FATAL_ERROR "Explicit static LLVM component archives required, not monolithic/import LLVM: ${CLAW_LLVM_LIB}")
    endif()
endforeach()
separate_arguments(CLAW_LLVM_SYSTEM_LIBS_LIST NATIVE_COMMAND "${CLAW_LLVM_SYSTEM_LIBS}")

# Keep all SDK headers, compile definitions/options, static LLVM archives, and
# system libraries private to the adapter.  In particular, do not expose LLVM
# headers through adapter.h or a PUBLIC link interface.
add_library(claw_llvm_cpp_sdk INTERFACE)
target_include_directories(claw_llvm_cpp_sdk SYSTEM INTERFACE "${CLAW_LLVM_INCLUDEDIR}")
if(WIN32)
    target_compile_definitions(claw_llvm_cpp_sdk INTERFACE LLVM_BUILD_STATIC=1)
endif()
if(CLAW_LLVM_HAS_RTTI STREQUAL "NO")
    target_compile_options(claw_llvm_cpp_sdk INTERFACE "-fno-rtti")
elseif(NOT CLAW_LLVM_HAS_RTTI STREQUAL "YES")
    message(FATAL_ERROR "Unrecognized LLVM RTTI contract: '${CLAW_LLVM_HAS_RTTI}'")
endif()
set(CLAW_LLVM_CPPFLAGS_LIST)
separate_arguments(CLAW_LLVM_CPPFLAGS_LIST NATIVE_COMMAND "${CLAW_LLVM_CPPFLAGS}")
foreach(CLAW_LLVM_CPPFLAG IN LISTS CLAW_LLVM_CPPFLAGS_LIST)
    if(CLAW_LLVM_CPPFLAG MATCHES "^-fno-exceptions$")
        message(FATAL_ERROR "LLVM llvm-config requested -fno-exceptions, which this exception-using adapter forbids")
    elseif(CLAW_LLVM_CPPFLAG MATCHES "^-D(.+)$")
        target_compile_definitions(claw_llvm_cpp_sdk INTERFACE "${CMAKE_MATCH_1}")
    elseif(NOT CLAW_LLVM_CPPFLAG MATCHES "^-I")
        target_compile_options(claw_llvm_cpp_sdk INTERFACE "${CLAW_LLVM_CPPFLAG}")
    endif()
endforeach()
target_link_libraries(claw_llvm_cpp_sdk INTERFACE
    "-Wl,--start-group" ${CLAW_LLVM_LIBFILES_LIST} "-Wl,--end-group"
    ${CLAW_LLVM_SYSTEM_LIBS_LIST})
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/llvm-sdk-provenance.txt"
    "platform=${CLAW_LLVM_POC_PLATFORM}\nllvm-config=${CLAW_LLVM_CONFIG}\nversion=${CLAW_LLVM_VERSION}\nheaders=${CLAW_LLVM_INCLUDEDIR}\nhost-target=${CLAW_LLVM_HOST_TARGET}\nhas-rtti=${CLAW_LLVM_HAS_RTTI}\nrequired-components=core codegen mc target x86codegen x86desc x86info\nstatic-libraries=${CLAW_LLVM_LIBFILES}\nsystem-libraries=${CLAW_LLVM_SYSTEM_LIBS}\n")
message(STATUS "PoC SDK: exact ${CLAW_LLVM_VERSION}, ${CLAW_LLVM_HOST_TARGET}, static X86 components, RTTI=${CLAW_LLVM_HAS_RTTI}")

add_library(claw_llvm_cpp_adapter adapter.cpp)
target_include_directories(claw_llvm_cpp_adapter PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}")
target_link_libraries(claw_llvm_cpp_adapter PRIVATE claw_llvm_cpp_sdk claw_frontend)
if(WIN32)
    target_compile_definitions(claw_llvm_cpp_adapter PRIVATE CLAW_LLVM_CPP_POC_UCRT_X64=1)
endif()

target_compile_features(claw_llvm_cpp_adapter PRIVATE cxx_std_20)

add_executable(claw-llvm-poc driver.cpp)
target_link_libraries(claw-llvm-poc PRIVATE claw_llvm_cpp_adapter)

# This executable is deliberately linked against the same explicit SDK closure;
# its CTest invocation proves that headers, archives, and runtime all agree.
set(CLAW_LLVM_VERSION_PROBE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/llvm_sdk_version_probe.cpp")
file(WRITE "${CLAW_LLVM_VERSION_PROBE_SOURCE}" [=[
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
int main() {
    llvm::cl::PrintVersionMessage();
    llvm::outs() << "LLVM_VERSION_STRING=" << LLVM_VERSION_STRING << '\n';
    return 0;
}
]=])
add_executable(claw-llvm-sdk-version-probe "${CLAW_LLVM_VERSION_PROBE_SOURCE}")
target_link_libraries(claw-llvm-sdk-version-probe PRIVATE claw_llvm_cpp_sdk)

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(CLAW_POC_CC NAMES cc clang REQUIRED)
    # Test-only production singleton evidence. This does not link into the
    # adapter, change production lowering, or grant aggregate acceptance.
    find_program(CLAW_SINGLETON_CLANG NAMES clang REQUIRED)
    find_program(CLAW_SINGLETON_GIT NAMES git
        HINTS "$ENV{ProgramFiles}/Git/cmd" "$ENV{ProgramW6432}/Git/cmd"
              "C:/Program Files/Git/cmd" "C:/Program Files/Git/bin" REQUIRED)
    execute_process(COMMAND "${CLAW_SINGLETON_GIT}" -C "${PROJECT_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE _singleton_source_sha OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _singleton_git_status)
    if(NOT _singleton_git_status EQUAL 0 OR NOT _singleton_source_sha MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "Singleton evidence requires a verifiable Git source revision")
    endif()
    add_executable(claw-singleton-layout-probe singleton_layout_probe.cpp)
    target_link_libraries(claw-singleton-layout-probe PRIVATE claw_frontend claw_llvm_cpp_sdk)
    add_test(NAME llvm-cpp-production-singleton-roundtrip
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_singleton_production.py"
            --production "$<TARGET_FILE:claw>" --layout-probe "$<TARGET_FILE:claw-singleton-layout-probe>"
            --cc "${CLAW_SINGLETON_CLANG}" --git "${CLAW_SINGLETON_GIT}"
            --expected-sha "${_singleton_source_sha}" --profile "${CLAW_LLVM_POC_PLATFORM}"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/singleton-artifacts/production")
    set_tests_properties(llvm-cpp-production-singleton-roundtrip PROPERTIES TIMEOUT 180)
    add_test(NAME llvm-cpp-singleton-rejection
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_singleton_rejection.py"
            --emitter "$<TARGET_FILE:claw-llvm-poc>"
            --fixture "${PROJECT_SOURCE_DIR}/tests/native/singleton_roundtrip.cat"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/singleton-artifacts/rejection")
    set_tests_properties(llvm-cpp-singleton-rejection PROPERTIES TIMEOUT 180)
    add_executable(claw-llvm-poc-adapter-tests test_adapter.cpp)
    target_link_libraries(claw-llvm-poc-adapter-tests PRIVATE claw_llvm_cpp_adapter)
    add_executable(claw-local-binding-identity-tests test_local_binding_identity.cpp)
    target_link_libraries(claw-local-binding-identity-tests PRIVATE claw_frontend)
    add_test(NAME llvm-cpp-local-binding-keys-metadata
        COMMAND claw-local-binding-identity-tests "${CMAKE_CURRENT_BINARY_DIR}/local-binding-metadata")
    add_executable(claw-tuple-cleanup-provenance-tests test_tuple_cleanup_provenance.cpp)
    target_link_libraries(claw-tuple-cleanup-provenance-tests PRIVATE claw_frontend)
    add_test(NAME llvm-cpp-tuple-cleanup-provenance
        COMMAND claw-tuple-cleanup-provenance-tests "${CMAKE_CURRENT_BINARY_DIR}/tuple-cleanup-provenance")
    add_executable(claw-parameter-binding-identity-tests test_parameter_binding_identity.cpp)
    target_link_libraries(claw-parameter-binding-identity-tests PRIVATE claw_frontend)
    add_test(NAME llvm-cpp-parameter-binding-keys-metadata
        COMMAND claw-parameter-binding-identity-tests "${CMAKE_CURRENT_BINARY_DIR}/parameter-binding-metadata")
    add_test(NAME llvm-cpp-sdk-version
        COMMAND claw-llvm-sdk-version-probe)
    set_tests_properties(llvm-cpp-sdk-version PROPERTIES
        PASS_REGULAR_EXPRESSION "LLVM version 23\\.1\\.3[\r\n]")
    add_test(NAME llvm-cpp-sdk-configure-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_sdk.py"
            --cmake "${CMAKE_COMMAND}" --source-dir "${PROJECT_SOURCE_DIR}"
            --generator "${CMAKE_GENERATOR}" --cxx-compiler "${CMAKE_CXX_COMPILER}")
    add_test(NAME llvm-cpp-poc-adapter
        COMMAND claw-llvm-poc-adapter-tests "${CMAKE_CURRENT_BINARY_DIR}/adapter-artifacts")
    add_test(NAME llvm-cpp-poc-differential
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_poc.py"
            --poc "$<TARGET_FILE:claw-llvm-poc>"
            --claw "$<TARGET_FILE:claw>"
            --cc "${CLAW_POC_CC}"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
endif()

if(CLAW_ENABLE_LLD_POC)
    add_subdirectory(lld)
endif()
