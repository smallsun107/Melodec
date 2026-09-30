# rencm - NCM (*.ncm) decoder library.
#
# Self-contained: no third-party dependencies, so this directory can be dropped
# into another project with add_subdirectory(rencm) and linked as rencm::rencm.
#
# Public headers live in include/rencm (namespace `rencm`); everything in src/
# (aes/rc4/key) is internal.

add_library(rencm STATIC
  src/format.cpp
  src/aes.cpp
  src/rc4.cpp
  src/key.cpp
  src/meta.cpp
  src/decoder.cpp)
add_library(rencm::rencm ALIAS rencm)

target_include_directories(rencm
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(rencm PUBLIC cxx_std_17)
