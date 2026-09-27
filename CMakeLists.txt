cmake_minimum_required(VERSION 3.24)

project(xeus-flang-repl VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(MLIR REQUIRED CONFIG)
find_package(Flang REQUIRED CONFIG)
find_package(xeus 6.0 REQUIRED CONFIG)
find_package(xeus-zmq 4.0 REQUIRED CONFIG)

set(XEUS_FLANG_SOURCE_DIR "" CACHE PATH
    "Optional LLVM source tree used with a non-installed Flang build")
set(XEUS_FLANG_RESOURCE_DIR "" CACHE PATH
    "Flang resource directory containing finclude/ and lib/")
set(XEUS_FLANG_OPENMP_RUNTIME "" CACHE FILEPATH
    "Optional libomp shared library used by the OpenMP kernelspec")
if(NOT XEUS_FLANG_RESOURCE_DIR)
  get_filename_component(_flang_build_dir "${Flang_DIR}/../../.." ABSOLUTE)
  set(XEUS_FLANG_RESOURCE_DIR
      "${_flang_build_dir}/lib/clang/${LLVM_VERSION_MAJOR}")
endif()

add_executable(xflang
  src/main.cpp
  src/xinterpreter.cpp
)

target_include_directories(xflang PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  $<$<BOOL:${XEUS_FLANG_SOURCE_DIR}>:${XEUS_FLANG_SOURCE_DIR}/flang/include>
)

target_include_directories(xflang SYSTEM PRIVATE
  ${FLANG_INCLUDE_DIRS}
  ${MLIR_INCLUDE_DIRS}
  ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(xflang PRIVATE
  XEUS_FLANG_VERSION="${PROJECT_VERSION}"
  XEUS_FLANG_DEFAULT_RESOURCE_DIR="${XEUS_FLANG_RESOURCE_DIR}"
)

target_link_libraries(xflang PRIVATE
  flangInterpreter
  xeus-zmq
)

if(APPLE)
  set_target_properties(xflang PROPERTIES
    BUILD_RPATH "${CMAKE_PREFIX_PATH}/lib"
    INSTALL_RPATH "${CMAKE_PREFIX_PATH}/lib;@loader_path/../${CMAKE_INSTALL_LIBDIR}"
  )
endif()

configure_file(
  share/jupyter/kernels/xflang/kernel.json.in
  ${CMAKE_CURRENT_BINARY_DIR}/share/jupyter/kernels/xflang/kernel.json
  @ONLY
)

install(TARGETS xflang RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/share/jupyter/kernels/xflang/kernel.json
  ${CMAKE_CURRENT_SOURCE_DIR}/share/jupyter/kernels/xflang/logo-32x32.png
  ${CMAKE_CURRENT_SOURCE_DIR}/share/jupyter/kernels/xflang/logo-64x64.png
  ${CMAKE_CURRENT_SOURCE_DIR}/share/jupyter/kernels/xflang/logo-svg.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/jupyter/kernels/xflang
)

if(XEUS_FLANG_OPENMP_RUNTIME)
  configure_file(
    share/jupyter/kernels/xflang-openmp/kernel.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/share/jupyter/kernels/xflang-openmp/kernel.json
    @ONLY
  )
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/share/jupyter/kernels/xflang-openmp/kernel.json
    ${CMAKE_CURRENT_SOURCE_DIR}/share/jupyter/kernels/xflang/logo-32x32.png
    ${CMAKE_CURRENT_SOURCE_DIR}/share/jupyter/kernels/xflang/logo-64x64.png
    ${CMAKE_CURRENT_SOURCE_DIR}/share/jupyter/kernels/xflang/logo-svg.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/jupyter/kernels/xflang-openmp
  )
endif()
