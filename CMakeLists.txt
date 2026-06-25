cmake_minimum_required(VERSION 3.25)
project(dune_daq_hdf VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wpedantic -Werror)

# Pure DUNE DAQ HDF5 reader: raw HDF5 + the header-only dune-daq-types. No Arrow,
# no WCT, no DUNE libraries.
find_package(HDF5 REQUIRED COMPONENTS C)
find_package(dune_daq_types CONFIG REQUIRED)

# Link the Spack view's gcc15 libstdc++ explicitly (GLIBCXX/CXXABI resolution),
# as in arrow-hdf.
# Resolve THIS compiler's libstdc++ directly, independent of CMAKE_PREFIX_PATH
# order. A Spack build can place an older gcc-runtime earlier on the path; the
# previous prefix scan grabbed that, linking gcc@15 objects against gcc@11
# libstdc++ (missing GLIBCXX_3.4.31/.32). The compiler always yields its own.
execute_process(
  COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.so
  OUTPUT_VARIABLE _stdc_real OUTPUT_STRIP_TRAILING_WHITESPACE)
file(REAL_PATH "${_stdc_real}" _stdc_real)
get_filename_component(_stdc_dir "${_stdc_real}" DIRECTORY)
set(DDH_RUNTIME_LIBS "${_stdc_real}")
link_directories(BEFORE "${_stdc_dir}")
string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,-rpath,${_stdc_dir}")

add_library(dune_daq_hdf SHARED dune_daq_hdf/DaqHdf5File.cpp)
target_include_directories(dune_daq_hdf
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(dune_daq_hdf
  PUBLIC
    dune_daq_types::dune_daq_types
    HDF5::HDF5
    ${DDH_RUNTIME_LIBS})

enable_testing()
add_subdirectory(test)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS dune_daq_hdf EXPORT dune_daq_hdfTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY dune_daq_hdf/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/dune_daq_hdf"
  FILES_MATCHING PATTERN "*.hpp")
install(EXPORT dune_daq_hdfTargets
  FILE dune_daq_hdfTargets.cmake
  NAMESPACE dune_daq_hdf::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dune_daq_hdf")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/dune_daq_hdfConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dune_daq_hdfConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/dune_daq_hdfConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dune_daq_hdf")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/dune_daq_hdfConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/dune_daq_hdfConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dune_daq_hdf")
