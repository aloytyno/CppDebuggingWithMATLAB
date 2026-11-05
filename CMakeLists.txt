cmake_minimum_required(VERSION 3.20)

project(nr_ofdm_debug_demo LANGUAGES CXX)

set(MATLAB_ROOT "C:/Program Files/MATLAB/R2026b_full" CACHE PATH
    "MATLAB installation root containing the C++ Engine API")

if(NOT EXISTS "${MATLAB_ROOT}/extern/include/MatlabEngine.hpp")
    message(FATAL_ERROR
        "MATLAB C++ Engine API not found. Set MATLAB_ROOT to your MATLAB installation.")
endif()

add_executable(nr_ofdm_debug_demo main.cpp)
target_compile_features(nr_ofdm_debug_demo PRIVATE cxx_std_17)
target_compile_definitions(nr_ofdm_debug_demo PRIVATE _CRT_SECURE_NO_WARNINGS)
target_include_directories(nr_ofdm_debug_demo PRIVATE
    "${MATLAB_ROOT}/extern/include")
target_link_libraries(nr_ofdm_debug_demo PRIVATE
    "${MATLAB_ROOT}/extern/lib/win64/microsoft/libMatlabEngine.lib"
    "${MATLAB_ROOT}/extern/lib/win64/microsoft/libMatlabDataArray.lib")

enable_testing()
add_test(NAME nr_ofdm_pipeline_self_test
         COMMAND nr_ofdm_debug_demo --self-test)
set_tests_properties(nr_ofdm_pipeline_self_test PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MATLAB_ROOT}/bin/win64;PATH=path_list_prepend:${MATLAB_ROOT}/extern/bin/win64")
