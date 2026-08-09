cmake_minimum_required(VERSION 3.30)

project(tool_sdf_ride_editor LANGUAGES CXX)

option(SDF_BUILD_TESTS "Build sdf generator tests" ON)
option(SDF_ENABLE_OPENMP "Enable OpenMP acceleration for CPU-heavy loops" ON)
option(SDF_BUILD_LUA_MODULE "Build the sdf-generator Lua module" ON)

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

if(SDF_BUILD_LUA_MODULE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  find_file(SDF_FABGEN_BIND_PY
    NAMES bind.py
    PATHS
      "C:/works/dev/harfang/FABGen"
      "C:/works/dev/harfang-evolution/fabgen"
    NO_DEFAULT_PATH)

  if(NOT SDF_FABGEN_BIND_PY)
    message(FATAL_ERROR "Fabgen bind.py was not found. Set SDF_BUILD_LUA_MODULE=OFF or install Fabgen locally.")
  endif()

  get_filename_component(SDF_FABGEN_ROOT "${SDF_FABGEN_BIND_PY}" DIRECTORY)

  find_path(SDF_LUA_INCLUDE_DIR
    NAMES lua.h lauxlib.h lualib.h
    PATHS
      "C:/works/dev/harfang/harfang3d/extern/lua/src"
      "C:/works/dev/harfang-evolution/harfang3d/extern/lua/src"
      "C:/works/projects/harfang3d/extern/lua/src"
      "C:/works/projects/lib-SAM/lua/src"
      "C:/works/projects/lib-say/lua/src"
    NO_DEFAULT_PATH)

  find_library(SDF_LUA_LIBRARY
    NAMES lua54 lua
    PATHS
      "C:/works/dev/harfang/build/lua-cmake/extern/lua/Release"
      "C:/works/dev/harfang/build/xm-player/extern/lua/Release"
      "C:/works/dev/harfang-evolution/build/extern/lua/Release"
      "C:/works/dev/harfang-evolution/build/extern/lua/RelWithDebInfo"
      "C:/works/projects/lib-SAM/build/lua/Release"
      "C:/works/projects/lib-say/bin/lua"
    NO_DEFAULT_PATH)

  if(NOT SDF_LUA_INCLUDE_DIR OR NOT SDF_LUA_LIBRARY)
    message(FATAL_ERROR "Lua 5.4 headers or import library were not found. Set SDF_BUILD_LUA_MODULE=OFF or configure the local Lua paths.")
  endif()

  set(SDF_LUA_BINDING_SCRIPT "${CMAKE_SOURCE_DIR}/src/sdf_lua/bind_sdf_generator.py")
  set(SDF_LUA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/sdf_lua")
  set(SDF_LUA_GENERATED_CPP "${SDF_LUA_GENERATED_DIR}/bind_Lua.cpp")
  set(SDF_LUA_GENERATED_H "${SDF_LUA_GENERATED_DIR}/bind_Lua.h")
  set(SDF_LUA_GENERATED_FABGEN_H "${SDF_LUA_GENERATED_DIR}/fabgen.h")

  add_custom_command(
    OUTPUT
      "${SDF_LUA_GENERATED_CPP}"
      "${SDF_LUA_GENERATED_H}"
      "${SDF_LUA_GENERATED_FABGEN_H}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDF_LUA_GENERATED_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${SDF_FABGEN_BIND_PY}" "${SDF_LUA_BINDING_SCRIPT}" --lua --out "${SDF_LUA_GENERATED_DIR}"
    DEPENDS
      "${SDF_LUA_BINDING_SCRIPT}"
      src/sdf_lua/include/sdf/lua_api.h
      src/sdf_core/include/sdf/scene.h
      src/sdf_core/include/sdf/scene_io.h
      src/sdf_core/include/sdf/types.h
      src/sdf_core/include/sdf/uv_unwrap.h
    WORKING_DIRECTORY "${SDF_FABGEN_ROOT}"
    VERBATIM)

  add_library(sdf_generator MODULE
    "${SDF_LUA_GENERATED_CPP}"
    src/sdf_lua/src/lua_api.cpp
  )

  target_include_directories(sdf_generator
    PRIVATE
      "${SDF_LUA_GENERATED_DIR}"
      src/sdf_core/include
      src/sdf_lua/include
      "${SDF_LUA_INCLUDE_DIR}")

  target_link_libraries(sdf_generator
    PRIVATE
      sdf_core
      "${SDF_LUA_LIBRARY}")

  target_compile_features(sdf_generator PRIVATE cxx_std_20)

  if(MSVC)
    set_source_files_properties("${SDF_LUA_GENERATED_CPP}" PROPERTIES COMPILE_OPTIONS "/W0")
  else()
    set_source_files_properties("${SDF_LUA_GENERATED_CPP}" PROPERTIES COMPILE_OPTIONS "-w")
  endif()

  set_target_properties(sdf_generator PROPERTIES
    PREFIX ""
    OUTPUT_NAME "sdf-generator"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

  add_custom_command(TARGET sdf_generator POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:sdf_generator>"
      "${CMAKE_SOURCE_DIR}/app/$<TARGET_FILE_NAME:sdf_generator>")

  sdf_apply_warnings(sdf_generator)
endif()

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
