cmake_minimum_required(VERSION 3.22)

project(GPUDirectFabric
  VERSION 1.0.0
  DESCRIPTION "Vendor-neutral runtime for governing accelerator direct-path eligibility and authority"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include(CTest)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GDF_BUILD_CUDA_BACKEND "Build the real CUDA backend (requires CUDA toolkit)" ON)
option(GDF_BUILD_RDMA_BACKEND "Build the RDMA/verbs backend (only where supported)" OFF)
option(GDF_BUILD_STORAGE_BACKEND "Build the storage/GDS backend (only where supported)" OFF)
option(GDF_BUILD_SYNTHETIC_BACKEND "Build the synthetic backend" ON)
option(GDF_BUILD_UNSUPPORTED_BACKEND "Build the no-op unsupported backend" ON)
option(GDF_BUILD_TESTS "Build tests" ON)
option(GDF_BUILD_EXAMPLES "Build examples" ON)
option(GDF_BUILD_CLI "Build the inspection CLI" ON)
option(GDF_BUILD_BENCH "Build benchmarks" ON)

# ---------------------------------------------------------------------------
# Warnings / strict policies
# ---------------------------------------------------------------------------
function(gdf_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Core library (vendor-neutral; no external dependencies)
# ---------------------------------------------------------------------------
set(GDF_CORE_SOURCES
  src/atomic_file.cpp
  src/backend.cpp
  src/enums.cpp
  src/path.cpp
  src/coordinator.cpp
  src/coordinator_lifecycle.cpp
  src/persistence.cpp
)

add_library(gdf_core STATIC ${GDF_CORE_SOURCES})
add_library(GPUDirectFabric::core ALIAS gdf_core)
target_compile_features(gdf_core PUBLIC cxx_std_20)
target_include_directories(gdf_core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
gdf_set_warnings(gdf_core)
# ---------------------------------------------------------------------------
# Synthetic backend (optional but always available)
# ---------------------------------------------------------------------------
if(GDF_BUILD_SYNTHETIC_BACKEND)
  add_library(gdf_synthetic STATIC src/backends/synthetic_backend.cpp)
  add_library(GPUDirectFabric::synthetic ALIAS gdf_synthetic)
  target_compile_features(gdf_synthetic PUBLIC cxx_std_20)
  target_link_libraries(gdf_synthetic PUBLIC gdf_core)
  target_include_directories(gdf_synthetic
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  gdf_set_warnings(gdf_synthetic)
endif()

# ---------------------------------------------------------------------------
# Multiprocess transport runtime (framed protocol + worker/coordinator)
# Kept separate so gdf_core stays vendor-neutral (no Winsock dependency).
# ---------------------------------------------------------------------------
add_library(gdf_mp STATIC
  src/protocol.cpp
  src/transport.cpp
  src/proc.cpp
  src/coordinator_server.cpp
  src/worker_client.cpp)
add_library(GPUDirectFabric::mp ALIAS gdf_mp)
target_compile_features(gdf_mp PUBLIC cxx_std_20)
target_link_libraries(gdf_mp PUBLIC gdf_core)
if(WIN32)
  target_link_libraries(gdf_mp PRIVATE ws2_32)
endif()
target_include_directories(gdf_mp
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
gdf_set_warnings(gdf_mp)

# ---------------------------------------------------------------------------
# Unsupported / no-op backend (honest UNSUPPORTED RDMA/storage representation)
# ---------------------------------------------------------------------------
if(GDF_BUILD_UNSUPPORTED_BACKEND)
  add_library(gdf_unsupported STATIC src/backends/unsupported_backend.cpp)
  add_library(GPUDirectFabric::unsupported ALIAS gdf_unsupported)
  target_compile_features(gdf_unsupported PUBLIC cxx_std_20)
  target_link_libraries(gdf_unsupported PUBLIC gdf_core)
  target_include_directories(gdf_unsupported
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  gdf_set_warnings(gdf_unsupported)
endif()

# ---------------------------------------------------------------------------
# CUDA backend (real, optional; requires CUDA toolkit)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
if(GDF_BUILD_CUDA_BACKEND)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
      if(NOT CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES "native")
      endif()
      add_library(gdf_cuda STATIC src/backends/cuda_backend.cpp src/backends/cuda_raw.cu)
      add_library(GPUDirectFabric::cuda ALIAS gdf_cuda)
      target_compile_features(gdf_cuda PUBLIC cxx_std_20)
      target_link_libraries(gdf_cuda PUBLIC gdf_core CUDA::cudart)
      target_include_directories(gdf_cuda
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
      set_target_properties(gdf_cuda PROPERTIES CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
      set_property(TARGET gdf_cuda PROPERTY CUDA_STANDARD 20)
      if(MSVC)
        target_compile_options(gdf_cuda PRIVATE
          $<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->
          $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4;-Xcompiler=/WX;-Wno-deprecated-gpu-targets>)
      else()
        target_compile_options(gdf_cuda PRIVATE -Wall -Wextra -Wpedantic -Werror)
      endif()
    else()
      message(STATUS "CUDA backend disabled: CUDA toolkit not found")
      set(GDF_BUILD_CUDA_BACKEND OFF CACHE BOOL "" FORCE)
    endif()
  else()
    message(STATUS "CUDA backend disabled: no CUDA compiler found")
    set(GDF_BUILD_CUDA_BACKEND OFF CACHE BOOL "" FORCE)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GDF_BUILD_TESTS)
  enable_testing()
  add_executable(gdf_smoke tests/smoke.cpp)
  target_link_libraries(gdf_smoke PRIVATE gdf_core)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gdf_smoke PRIVATE gdf_synthetic)
  endif()
  gdf_set_warnings(gdf_smoke)
  add_test(NAME smoke COMMAND gdf_smoke)

  if(GDF_BUILD_CUDA_BACKEND AND TARGET gdf_cuda)
    add_executable(gdf_cuda_validation_test tests/cuda_validation_test.cpp)
    target_link_libraries(gdf_cuda_validation_test PRIVATE gdf_cuda)
    gdf_set_warnings(gdf_cuda_validation_test)
    add_test(NAME cuda_validation COMMAND gdf_cuda_validation_test)
  endif()

  add_executable(gdf_worker_death_test tests/worker_death_test.cpp)
  target_link_libraries(gdf_worker_death_test PRIVATE gdf_mp)
  gdf_set_warnings(gdf_worker_death_test)
  add_test(NAME worker_death COMMAND gdf_worker_death_test ${CMAKE_CURRENT_BINARY_DIR})

  add_executable(gdf_coordinator_restart_test tests/coordinator_restart_test.cpp)
  target_link_libraries(gdf_coordinator_restart_test PRIVATE gdf_core)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gdf_coordinator_restart_test PRIVATE gdf_synthetic)
  endif()
  gdf_set_warnings(gdf_coordinator_restart_test)
  add_test(NAME coordinator_restart COMMAND gdf_coordinator_restart_test)

  add_executable(gdf_core_test tests/core_test.cpp)
  target_link_libraries(gdf_core_test PRIVATE gdf_core gdf_mp)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gdf_core_test PRIVATE gdf_synthetic)
  endif()
  gdf_set_warnings(gdf_core_test)
  add_test(NAME core COMMAND gdf_core_test)

  add_executable(gdf_property_test tests/property_test.cpp)
  target_link_libraries(gdf_property_test PRIVATE gdf_core)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gdf_property_test PRIVATE gdf_synthetic)
  endif()
  gdf_set_warnings(gdf_property_test)
  add_test(NAME property COMMAND gdf_property_test)

  add_executable(gdf_race_test tests/race_test.cpp)
  target_link_libraries(gdf_race_test PRIVATE gdf_core)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gdf_race_test PRIVATE gdf_synthetic)
  endif()
  gdf_set_warnings(gdf_race_test)
  add_test(NAME race COMMAND gdf_race_test)
endif()
# ---------------------------------------------------------------------------
# Coordinator + worker executables (multiprocess authority)
# ---------------------------------------------------------------------------
add_executable(gdf_coordinator tools/coordinator_main.cpp)
target_link_libraries(gdf_coordinator PRIVATE gdf_mp)
gdf_set_warnings(gdf_coordinator)

add_executable(gdf_worker tools/worker_main.cpp)
target_link_libraries(gdf_worker PRIVATE gdf_mp)
gdf_set_warnings(gdf_worker)
# ---------------------------------------------------------------------------
# Inspection CLI
# ---------------------------------------------------------------------------
if(GDF_BUILD_CLI)
  add_executable(gpu_direct_fabric_inspect tools/inspect_main.cpp)
  target_link_libraries(gpu_direct_fabric_inspect PRIVATE gdf_core)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gpu_direct_fabric_inspect PRIVATE gdf_synthetic)
  endif()
  if(GDF_BUILD_CUDA_BACKEND AND TARGET gdf_cuda)
    target_link_libraries(gpu_direct_fabric_inspect PRIVATE gdf_cuda)
  endif()
  gdf_set_warnings(gpu_direct_fabric_inspect)
endif()

# ---------------------------------------------------------------------------
# Examples (installed-API)
# ---------------------------------------------------------------------------
if(GDF_BUILD_EXAMPLES)
  add_executable(gdf_example_direct_decision examples/example_direct_decision.cpp)
  target_link_libraries(gdf_example_direct_decision PRIVATE gdf_core)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gdf_example_direct_decision PRIVATE gdf_synthetic)
  endif()
  gdf_set_warnings(gdf_example_direct_decision)

  add_executable(gdf_example_stale_fencing examples/example_stale_fencing.cpp)
  target_link_libraries(gdf_example_stale_fencing PRIVATE gdf_core)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gdf_example_stale_fencing PRIVATE gdf_synthetic)
  endif()
  gdf_set_warnings(gdf_example_stale_fencing)

  add_executable(gdf_example_cuda_proof examples/example_cuda_proof.cpp)
  target_link_libraries(gdf_example_cuda_proof PRIVATE gdf_core)
  if(GDF_BUILD_CUDA_BACKEND AND TARGET gdf_cuda)
    target_link_libraries(gdf_example_cuda_proof PRIVATE gdf_cuda)
  endif()
  gdf_set_warnings(gdf_example_cuda_proof)

  add_executable(gdf_example_unsupported examples/example_unsupported.cpp)
  target_link_libraries(gdf_example_unsupported PRIVATE gdf_core)
  if(GDF_BUILD_UNSUPPORTED_BACKEND)
    target_link_libraries(gdf_example_unsupported PRIVATE gdf_unsupported)
  endif()
  gdf_set_warnings(gdf_example_unsupported)
endif()

# ---------------------------------------------------------------------------
# Benchmarks (measure completed work)
# ---------------------------------------------------------------------------
if(GDF_BUILD_BENCH)
  add_executable(gdf_bench_control bench/bench_control.cpp)
  target_link_libraries(gdf_bench_control PRIVATE gdf_core)
  if(GDF_BUILD_SYNTHETIC_BACKEND)
    target_link_libraries(gdf_bench_control PRIVATE gdf_synthetic)
  endif()
  gdf_set_warnings(gdf_bench_control)
endif()

# ---------------------------------------------------------------------------
# Install / package
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
set(GDF_INSTALL_TARGETS gdf_core)
if(GDF_BUILD_SYNTHETIC_BACKEND)
  list(APPEND GDF_INSTALL_TARGETS gdf_synthetic)
endif()
if(GDF_BUILD_CUDA_BACKEND AND TARGET gdf_cuda)
  list(APPEND GDF_INSTALL_TARGETS gdf_cuda)
endif()
if(GDF_BUILD_CLI)
  list(APPEND GDF_INSTALL_TARGETS gpu_direct_fabric_inspect)
endif()
if(GDF_BUILD_UNSUPPORTED_BACKEND)
  list(APPEND GDF_INSTALL_TARGETS gdf_unsupported)
endif()

install(TARGETS ${GDF_INSTALL_TARGETS}
  EXPORT GPUDirectFabricTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT GPUDirectFabricTargets NAMESPACE GPUDirectFabric:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GPUDirectFabric)
set(GDF_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/GPUDirectFabric")
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GPUDirectFabricConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/GPUDirectFabricConfig.cmake"
  INSTALL_DESTINATION "${GDF_CMAKE_INSTALL_DIR}")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/GPUDirectFabricConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/GPUDirectFabricConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/GPUDirectFabricConfigVersion.cmake"
  DESTINATION "${GDF_CMAKE_INSTALL_DIR}")