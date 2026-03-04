cmake_minimum_required(VERSION 3.15)

project(nsmbw-compress C)

add_library(cx
  CXCompression.c
  CXSecureUncompression.c
  CXStreamingUncompression.c
  CXUncompression.c
)

add_executable(nsmbw-compress
  nsmbw_compress.c
)

target_compile_options(cx PRIVATE
  -O3
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-ffunction-sections -fdata-sections>
  $<$<CXX_COMPILER_ID:MSVC>:/Gy>
)

target_compile_options(nsmbw-compress PRIVATE
  -g
  -Og
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-ffunction-sections -fdata-sections>
  $<$<CXX_COMPILER_ID:MSVC>:/Gy>
)

target_link_options(nsmbw-compress PRIVATE
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-Wl,-dead_strip> # Apple
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-Wl,--gc-sections> # Clang/GCC
  $<$<CXX_COMPILER_ID:MSVC>:/OPT:REF>
)

target_link_libraries(nsmbw-compress cx)

