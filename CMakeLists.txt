cmake_minimum_required(VERSION 3.16)

project(Ryn
    VERSION 0.2.0
    DESCRIPTION "The Ryn programming language compiler"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(RYN_WERROR "Treat compiler warnings as errors" OFF)
option(RYN_BUILD_TESTS "Build the compiler test suite" ON)

function(ryn_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(RYN_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(RYN_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Every compiler stage lives in its own translation unit. main.c is kept out of
# the library so that tests can link the whole pipeline without a second main.
set(RYN_CORE_SOURCES
    src/util.c
    src/source.c
    src/diagnostics.c
    src/lexer.c
    src/ast.c
    src/types.c
    src/parser.c
    src/module.c
    src/builtins.c
    src/sema.c
    src/sema_expr.c
    src/sema_stmt.c
    src/ir.c
    src/codegen.c
    src/compiler.c
    src/package.c
)

add_library(ryn_core STATIC ${RYN_CORE_SOURCES})
target_include_directories(ryn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ryn_core PRIVATE
    RYN_RUNTIME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/runtime")
ryn_enable_warnings(ryn_core)

add_executable(ryn src/main.c)
target_link_libraries(ryn PRIVATE ryn_core)
ryn_enable_warnings(ryn)

add_executable(rynx src/rynx.c)
target_link_libraries(rynx PRIVATE ryn_core)
ryn_enable_warnings(rynx)

include(GNUInstallDirs)

install(TARGETS ryn rynx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY runtime/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ryn/runtime
        FILES_MATCHING PATTERN "*.h" PATTERN "*.c")
install(DIRECTORY docs/ examples/
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        OPTIONAL)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(RYN_BUILD_TESTS)
    enable_testing()

    add_executable(ryn_tests
        tests/test_main.c
        tests/test_util.c
        tests/test_lexer.c
        tests/test_parser.c
        tests/test_sema.c
        tests/test_codegen.c
        tests/test_package.c
        tests/test_module.c
    )
    target_link_libraries(ryn_tests PRIVATE ryn_core)
    target_include_directories(ryn_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    ryn_enable_warnings(ryn_tests)

    add_test(NAME ryn-unit COMMAND ryn_tests)
    add_test(NAME ryn-version COMMAND ryn version)
    add_test(NAME rynx-version COMMAND rynx --version)

    if(UNIX)
        add_test(NAME ryn-cli
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli_tests.sh
                         $<TARGET_FILE:ryn>
                         ${CMAKE_CURRENT_SOURCE_DIR}/runtime
                         ${CMAKE_CURRENT_SOURCE_DIR}/examples)
        add_test(NAME rynx-cli
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_rynx_tests.sh
                         $<TARGET_FILE:rynx>
                         $<TARGET_FILE:ryn>)
    endif()
endif()
