add_executable(rencm_tests rencm_tests.cpp)
target_link_libraries(rencm_tests PRIVATE rencm::rencm)
target_compile_features(rencm_tests PRIVATE cxx_std_17)

add_test(NAME rencm_tests COMMAND rencm_tests)
