cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(textochip_runtime)

# The portable core lives in ../src and is shared with the host build verbatim.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_sources(app PRIVATE
  src/main.cpp
  src/hal_zephyr.cpp        # the ONLY board-specific file
  ../src/isa.cpp
  ../src/vm.cpp
  ../src/runtime.cpp
  ../src/registry.cpp
)
