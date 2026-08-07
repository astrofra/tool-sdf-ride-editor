cmake_minimum_required(VERSION 3.30)

project(tool_sdf_ride_editor LANGUAGES CXX)

option(SDF_BUILD_TESTS "Build sdf generator tests" ON)
option(SDF_ENABLE_OPENMP "Enable OpenMP acceleration for CPU-heavy loops" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(sdf_apply_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(sdf_core
  src/sdf_core/src/bake_ao.cpp
  src/sdf_core/src/bake_normal.cpp
  src/sdf_core/src/bake_surface_pack.cpp
  src/sdf_core/src/debug_render.cpp
  src/sdf_core/src/generator.cpp
  src/sdf_core/src/image_write.cpp
  src/sdf_core/src/obj_writer.cpp
  src/sdf_core/src/raytrace.cpp
  src/sdf_core/src/scene.cpp
  src/sdf_core/src/scene_io.cpp
  src/sdf_core/src/uv_unwrap.cpp
  vendor/xatlas/xatlas.cpp
)

if(SDF_ENABLE_OPENMP)
  find_package(OpenMP QUIET)
endif()

target_include_directories(sdf_core
  PUBLIC
    src/sdf_core/include
  PRIVATE
    vendor/stb
    vendor/xatlas
)

if(MSVC)
  set_source_files_properties(vendor/xatlas/xatlas.cpp PROPERTIES
    COMPILE_DEFINITIONS "XA_DEBUG=0;NDEBUG"
    COMPILE_OPTIONS "/W0"
  )
else()
  set_source_files_properties(vendor/xatlas/xatlas.cpp PROPERTIES
    COMPILE_DEFINITIONS "XA_DEBUG=0;NDEBUG"
    COMPILE_OPTIONS "-w"
  )
endif()

target_compile_features(sdf_core PUBLIC cxx_std_20)

if(SDF_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
  target_link_libraries(sdf_core PRIVATE OpenMP::OpenMP_CXX)
endif()

set_target_properties(sdf_core PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

sdf_apply_warnings(sdf_core)

add_executable(sdf_cli
  src/sdf_cli/main.cpp
)

target_link_libraries(sdf_cli PRIVATE sdf_core)
target_compile_definitions(sdf_cli PRIVATE SDF_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

set_target_properties(sdf_cli PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

sdf_apply_warnings(sdf_cli)

if(SDF_BUILD_TESTS)
  enable_testing()

  add_executable(sdf_tests
    tests/test_main.cpp
  )

  target_link_libraries(sdf_tests PRIVATE sdf_core)
  target_compile_definitions(sdf_tests PRIVATE SDF_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  set_target_properties(sdf_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  sdf_apply_warnings(sdf_tests)

  add_test(NAME sdf_tests COMMAND sdf_tests)
endif()
