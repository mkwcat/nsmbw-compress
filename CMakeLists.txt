cmake_minimum_required(VERSION 3.15)

project(nsmbw-compress C)

add_executable(nsmbw-compress
  nsmbw_compress.c
  lz.c
  huff.c
  rl.c
  lh.c
  lrc.c
  filter_diff.c
  szs.c
  test.c
)
