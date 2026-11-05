# Configure with [fxsdk build-fx], which provides the toolchain file and
# module path of the fxSDK. Builds a .g1a for fx-9860G-series calculators
# (fx-9750GIII included).

cmake_minimum_required(VERSION 3.15)
project(fxj2me)

include(GenerateG1A)
include(Fxconv)
find_package(Gint 2.9 REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(VM_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
set(LIB_JAVA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../lib/java)

# Boot class library: javac compiles it, then a host-built tool prelinks it
# into a const ROM image (tools/gen_bootimg.c). The image is flash-resident
# like any other const data, and loading a boot class costs only its jclass
# header plus static storage -- no jar parsing, no class metadata in RAM.
find_program(HOST_CC NAMES cc gcc clang)
if(NOT HOST_CC)
  message(FATAL_ERROR "gen_bootimg needs a host C compiler (cc/gcc/clang)")
endif()

file(GLOB_RECURSE BOOT_SOURCES ${LIB_JAVA_DIR}/*.java)
set(BOOTIMG_TOOL ${CMAKE_CURRENT_BINARY_DIR}/gen_bootimg-host)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bootimg.c
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/build_boot.py
          ${LIB_JAVA_DIR} ${CMAKE_CURRENT_BINARY_DIR}/boot
  COMMAND ${HOST_CC} -O2 -I${VM_CORE_DIR} -o ${BOOTIMG_TOOL}
          ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_bootimg.c
          ${VM_CORE_DIR}/classfile.c ${VM_CORE_DIR}/jar.c
          ${VM_CORE_DIR}/inflate.c ${VM_CORE_DIR}/bootimg_null.c
  COMMAND ${BOOTIMG_TOOL} ${CMAKE_CURRENT_BINARY_DIR}/boot/boot.jar
          ${CMAKE_CURRENT_BINARY_DIR}/bootimg.c
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/build_boot.py
          ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_bootimg.c
          ${VM_CORE_DIR}/classfile.c ${VM_CORE_DIR}/jar.c
          ${VM_CORE_DIR}/inflate.c ${VM_CORE_DIR}/bootimg_null.c
          ${BOOT_SOURCES}
  COMMENT "Prelinking the boot class library into flash tables")

set(SOURCES
  src/main.c
  src/selftest.c
  src/vfs_posix.c
  src/test_jar_blob.c
  ${CMAKE_CURRENT_BINARY_DIR}/bootimg.c
  ${VM_CORE_DIR}/inflate.c
  ${VM_CORE_DIR}/jar.c
  ${VM_CORE_DIR}/mem.c
  ${VM_CORE_DIR}/classfile.c
  ${VM_CORE_DIR}/vm.c
  ${VM_CORE_DIR}/natives.c
  ${VM_CORE_DIR}/manifest.c
  ${VM_CORE_DIR}/midp.c
  ${VM_CORE_DIR}/draw.c
  ${VM_CORE_DIR}/font8x8.c
  ${VM_CORE_DIR}/fscfg.c
  ${VM_CORE_DIR}/scan.c
  ${VM_CORE_DIR}/ams.c
  ${VM_CORE_DIR}/pkg.c
)

add_executable(fxj2me ${SOURCES})
target_include_directories(fxj2me PRIVATE ${VM_CORE_DIR})
target_compile_options(fxj2me PRIVATE -Wall -Wextra -Os -g)
target_link_libraries(fxj2me Gint::Gint)

generate_g1a(TARGET fxj2me OUTPUT "fxj2me.g1a"
  NAME "fxj2me" ICON assets-fx/icon.png)
