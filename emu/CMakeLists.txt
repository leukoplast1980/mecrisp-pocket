cmake_minimum_required(VERSION 3.18)
project(mecrisp_pocket_emu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT ANDROID)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_library(qpcore STATIC src/rv32.c src/pocket.c)
target_include_directories(qpcore PUBLIC include PRIVATE src)
target_link_libraries(qpcore PUBLIC Threads::Threads)
set_target_properties(qpcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(qpcore PRIVATE -Wall -Wextra -O2)
endif()

if(NOT ANDROID)
  add_executable(qpemu cli/qpemu.c)
  target_link_libraries(qpemu PRIVATE qpcore)

  # Runs one riscv-tests ISA test binary on a bare RV32IM machine.
  add_executable(isa-runner ../tests/riscv/isa_runner.c)
  target_include_directories(isa-runner PRIVATE src)
  target_link_libraries(isa-runner PRIVATE qpcore)
endif()
