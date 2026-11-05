cmake_minimum_required(VERSION 3.16)
project(fxj2me C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -O2)

add_library(fxj2me_core STATIC
    src/core/inflate.c
    src/core/jar.c
    src/core/mem.c
    src/core/classfile.c
    src/core/vm.c
    src/core/natives.c
    src/core/manifest.c
    src/core/midp.c
    src/core/draw.c
    src/core/font8x8.c
    src/core/fscfg.c
    src/core/scan.c
    src/core/ams.c
    src/core/pkg.c
)
target_include_directories(fxj2me_core PUBLIC src/core)
target_link_libraries(fxj2me_core PUBLIC m)

# --- boot class library (javac + tools/build_boot.py) -------------------
find_package(Python3 COMPONENTS Interpreter)
set(FIXDIR ${CMAKE_BINARY_DIR}/fixtures)
set(BOOTDIR ${CMAKE_BINARY_DIR}/boot)

# The boot library is prelinked into a const ROM image at build time, so a
# launch does not re-parse it into RAM (tools/gen_bootimg.c).
if(Python3_FOUND)
    add_executable(gen_bootimg tools/gen_bootimg.c
        src/core/classfile.c src/core/jar.c src/core/inflate.c
        src/core/bootimg_null.c)
    target_include_directories(gen_bootimg PRIVATE src/core)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bootimg.c
        COMMAND gen_bootimg ${BOOTDIR}/boot.jar ${CMAKE_BINARY_DIR}/bootimg.c
        DEPENDS gen_bootimg ${BOOTDIR}/boot.jar
        COMMENT "Prelinking the boot class library into flash tables")
    target_sources(fxj2me_core PRIVATE ${CMAKE_BINARY_DIR}/bootimg.c)
else()
    target_sources(fxj2me_core PRIVATE src/core/bootimg_null.c)
endif()

if(Python3_FOUND)
    file(GLOB_RECURSE BOOT_SOURCES ${CMAKE_SOURCE_DIR}/lib/java/*.java)
    add_custom_command(
        OUTPUT ${BOOTDIR}/boot.jar
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_boot.py
                ${CMAKE_SOURCE_DIR}/lib/java ${BOOTDIR}
        DEPENDS tools/build_boot.py ${BOOT_SOURCES}
        COMMENT "Building boot class library")
    add_custom_target(bootlib ALL DEPENDS ${BOOTDIR}/boot.jar)

    # --- fixtures (deflate vectors + test classes + test.jar + hello.jar) -
    add_custom_command(
        OUTPUT ${FIXDIR}/.stamp
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/gen_fixtures.py
                ${FIXDIR} ${BOOTDIR}/boot.jar
        COMMAND ${CMAKE_COMMAND} -E touch ${FIXDIR}/.stamp
        DEPENDS tests/gen_fixtures.py tests/fixtures/Fib.java
                tests/fixtures/Runtime.java tests/fixtures/MissingSuper.java
                tests/fixtures/HelloMidlet.java
                ${BOOTDIR}/boot.jar
        COMMENT "Generating test fixtures")
    add_custom_target(fixtures ALL DEPENDS ${FIXDIR}/.stamp)
endif()

enable_testing()

# shell core tests (config/scan/install) with the host VFS backend
add_executable(test_shell tests/test_shell.c tests/hostvfs.c)
target_link_libraries(test_shell fxj2me_core)
add_test(NAME shell COMMAND test_shell)

# installed-suite package: unpacked jar read back from storage
add_executable(test_pkg tests/test_pkg.c tests/hostvfs.c)
target_link_libraries(test_pkg fxj2me_core)
if(Python3_FOUND)
    add_dependencies(test_pkg fixtures)
    add_test(NAME pkg COMMAND test_pkg ${FIXDIR})
else()
    add_test(NAME pkg COMMAND test_pkg)
endif()
set_tests_properties(pkg PROPERTIES SKIP_RETURN_CODE 77)
# Same suite with the device's short reads/writes simulated.
if(Python3_FOUND)
    add_test(NAME pkgchunk COMMAND test_pkg ${FIXDIR})
    set_tests_properties(pkgchunk PROPERTIES ENVIRONMENT "HOSTVFS_CHUNK=7"
                         SKIP_RETURN_CODE 77)
endif()

# application management: suite install/upgrade/JAD validation/uninstall
add_executable(test_ams tests/test_ams.c tests/hostvfs.c)
target_link_libraries(test_ams fxj2me_core)
if(Python3_FOUND)
    add_dependencies(test_ams fixtures)
    add_test(NAME ams COMMAND test_ams ${FIXDIR} ${BOOTDIR}/boot.jar)
    add_test(NAME amschunk COMMAND test_ams ${FIXDIR} ${BOOTDIR}/boot.jar)
    set_tests_properties(amschunk PROPERTIES ENVIRONMENT "HOSTVFS_CHUNK=5")
else()
    add_test(NAME ams COMMAND test_ams)
endif()

foreach(t inflate jar mem vm)
    add_executable(test_${t} tests/test_${t}.c tests/hostvfs.c)
    target_link_libraries(test_${t} fxj2me_core)
    if(Python3_FOUND)
        add_dependencies(test_${t} fixtures)
        add_test(NAME ${t} COMMAND test_${t} ${FIXDIR})
    else()
        add_test(NAME ${t} COMMAND test_${t})
    endif()
endforeach()

# runtime tests need the boot class library (strings/exceptions, MIDP)
foreach(t runtime midp koala)
    add_executable(test_${t} tests/test_${t}.c tests/hostvfs.c)
    target_link_libraries(test_${t} fxj2me_core)
    if(Python3_FOUND)
        add_dependencies(test_${t} bootlib fixtures)
    endif()
endforeach()
if(Python3_FOUND)
    add_test(NAME runtime COMMAND test_runtime
             ${BOOTDIR}/boot.jar ${FIXDIR}/test.jar)
    add_test(NAME midp COMMAND test_midp
             ${BOOTDIR}/boot.jar ${FIXDIR}/hello.jar)
    add_test(NAME koala COMMAND test_koala
             ${BOOTDIR}/boot.jar
             ${CMAKE_SOURCE_DIR}/tests/thirdparty/KoalaMines.jar)
    set_tests_properties(runtime midp koala PROPERTIES SKIP_RETURN_CODE 77)
endif()
