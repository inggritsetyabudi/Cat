# Parent has already validated native x64 platform, exact SDK headers and CLI.
claw_llvm_config(CLAW_LLD_LIBDIR --libdir)
claw_llvm_config(CLAW_LLD_BINDIR --bindir)
set(CLAW_LLD_EXECUTABLE "${CLAW_LLD_BINDIR}/ld.lld${CMAKE_EXECUTABLE_SUFFIX}" CACHE FILEPATH
    "Exact23.1.3 external GNU-flavor LLD; never a compiler driver")
if(NOT IS_ABSOLUTE "${CLAW_LLD_EXECUTABLE}" OR NOT EXISTS "${CLAW_LLD_EXECUTABLE}")
    message(FATAL_ERROR "LLD PoC requires explicit existing CLAW_LLD_EXECUTABLE from exact23.1.3 SDK")
endif()
execute_process(COMMAND "${CLAW_LLD_EXECUTABLE}" --version RESULT_VARIABLE _lld_status
    OUTPUT_VARIABLE _lld_version ERROR_VARIABLE _lld_error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _lld_status EQUAL 0 OR NOT _lld_version MATCHES "^LLD 23\\.1\\.3([ \r\n(]|$)")
    message(FATAL_ERROR "LLD PoC requires exact LLD 23.1.3, not '${_lld_version}${_lld_error}'")
endif()
if(NOT EXISTS "${CLAW_LLVM_INCLUDEDIR}/lld/Common/Driver.h")
    message(FATAL_ERROR "The same exact23.1.3 SDK must provide lld/Common/Driver.h")
endif()
set(CLAW_LLD_ARCHIVES)
foreach(_component lldELF lldCOFF lldMinGW lldCommon)
    set(_archive "${CLAW_LLD_LIBDIR}/lib${_component}.a")
    if(NOT EXISTS "${_archive}")
        message(FATAL_ERROR "Missing exact-SDK static LLD component ${_archive}; no executable-only fallback")
    endif()
    list(APPEND CLAW_LLD_ARCHIVES "${_archive}")
endforeach()
# Union from the exact23.1.3 upstream Common/ELF/COFF/MinGW CMakeLists.
# Upstream LLD initializes every SDK target and LTO. This is intentionally larger
# than the X86-only object emitter; it does NOT imply tested cross-link support.
set(CLAW_LLD_LLVM_COMPONENTS all-targets binaryformat bitwriter core
    debuginfodwarf debuginfocodeview debuginfomsf debuginfopdb demangle dtlto
    libdriver lto mc object option passes profiledata support target targetparser
    transformutils windowsdriver windowsmanifest codegen)
claw_llvm_config(CLAW_LLD_LLVM_LIBFILES --link-static --libfiles ${CLAW_LLD_LLVM_COMPONENTS})
claw_llvm_config(CLAW_LLD_SYSTEM_LIBS --link-static --system-libs ${CLAW_LLD_LLVM_COMPONENTS})
separate_arguments(CLAW_LLD_LLVM_LIBFILES_LIST NATIVE_COMMAND "${CLAW_LLD_LLVM_LIBFILES}")
separate_arguments(CLAW_LLD_SYSTEM_LIBS_LIST NATIVE_COMMAND "${CLAW_LLD_SYSTEM_LIBS}")
file(REAL_PATH "${CLAW_LLD_LIBDIR}" _sdk_real_libdir)
foreach(_archive IN LISTS CLAW_LLD_LLVM_LIBFILES_LIST)
    if(NOT IS_ABSOLUTE "${_archive}" OR NOT EXISTS "${_archive}" OR
       NOT _archive MATCHES "lib(LLVM[^/]+|Polly(ISL)?)\\.a$" OR _archive MATCHES "\\.dll\\.a$")
        message(FATAL_ERROR "Invalid exact-SDK LLVM component in LLD closure: ${_archive}")
    endif()
    get_filename_component(_parent "${_archive}" DIRECTORY)
    file(REAL_PATH "${_parent}" _parent_real)
    if(NOT _parent_real STREQUAL _sdk_real_libdir)
        message(FATAL_ERROR "LLD may not mix LLVM component prefixes: ${_archive}")
    endif()
endforeach()
add_executable(claw-lld-link-poc main.cpp)
target_include_directories(claw-lld-link-poc SYSTEM PRIVATE "${CLAW_LLVM_INCLUDEDIR}")
target_compile_features(claw-lld-link-poc PRIVATE cxx_std_20)
if(WIN32)
    target_compile_definitions(claw-lld-link-poc PRIVATE LLVM_BUILD_STATIC=1)
endif()
if(CLAW_LLVM_HAS_RTTI STREQUAL "NO")
    target_compile_options(claw-lld-link-poc PRIVATE -fno-rtti)
endif()
foreach(_flag IN LISTS CLAW_LLVM_CPPFLAGS_LIST)
    if(_flag MATCHES "^-D(.+)$")
        target_compile_definitions(claw-lld-link-poc PRIVATE "${CMAKE_MATCH_1}")
    elseif(NOT _flag MATCHES "^-I")
        target_compile_options(claw-lld-link-poc PRIVATE "${_flag}")
    endif()
endforeach()
target_link_libraries(claw-lld-link-poc PRIVATE
    "-Wl,--start-group" ${CLAW_LLD_ARCHIVES} ${CLAW_LLD_LLVM_LIBFILES_LIST}
    "-Wl,--end-group" ${CLAW_LLD_SYSTEM_LIBS_LIST} Threads::Threads)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lld-sdk-provenance.txt"
    "platform=${CLAW_LLVM_POC_PLATFORM}\nllvm-config=${CLAW_LLVM_CONFIG}\nheaders=${CLAW_LLVM_INCLUDEDIR}\nexternal-lld=${CLAW_LLD_EXECUTABLE}\nexternal-version=${_lld_version}\nlld-static-components=${CLAW_LLD_ARCHIVES}\nllvm-required-components=${CLAW_LLD_LLVM_COMPONENTS}\nllvm-static-closure=${CLAW_LLD_LLVM_LIBFILES}\nsystem-libraries=${CLAW_LLD_SYSTEM_LIBS}\n")
if(BUILD_TESTING)
    add_test(NAME llvm-cpp-lld-sdk-version COMMAND claw-lld-link-poc --sdk-version)
    set_tests_properties(llvm-cpp-lld-sdk-version PROPERTIES PASS_REGULAR_EXPRESSION "LLD_LIBRARY=LLD 23\\.1\\.3[ (]")
    add_test(NAME llvm-cpp-lld-serial-reentry COMMAND claw-lld-link-poc --poc-reentry-check)
    add_test(NAME llvm-cpp-lld-configure-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_config.py"
            --cmake "${CMAKE_COMMAND}" --source-dir "${PROJECT_SOURCE_DIR}"
            --llvm-config "${CLAW_LLVM_CONFIG}" --cxx-compiler "${CMAKE_CXX_COMPILER}"
            --generator "${CMAKE_GENERATOR}")
    set(_profile_args)
    if(WIN32)
        find_program(CLAW_LLD_READOBJ NAMES llvm-readobj REQUIRED)
        get_filename_component(_prefix "${CLAW_LLD_LIBDIR}" DIRECTORY)
        list(APPEND _profile_args --ucrt-prefix "${_prefix}" --readobj "${CLAW_LLD_READOBJ}")
    endif()
    add_test(NAME llvm-cpp-lld-native
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_lld.py"
            --embedded "$<TARGET_FILE:claw-lld-link-poc>" --external "${CLAW_LLD_EXECUTABLE}"
            --baseline-artifacts "${CMAKE_CURRENT_BINARY_DIR}/../artifacts"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts" ${_profile_args})
    set_tests_properties(llvm-cpp-lld-native PROPERTIES DEPENDS llvm-cpp-poc-differential TIMEOUT 180)
endif()
