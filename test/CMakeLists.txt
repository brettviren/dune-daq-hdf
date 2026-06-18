add_executable(test_read test_read.cpp)
target_link_libraries(test_read PRIVATE dune_daq_hdf HDF5::HDF5 ${DDH_RUNTIME_LIBS})
add_test(NAME read COMMAND test_read)

# Optional structural smoke test against a real DAQ file, if present.
set(_real "/devel/ddm/data/PDVD/np02vd_raw_run039349_0035_df-s03-d3_dw_0_20250909T125946.hdf5")
if(EXISTS "${_real}")
  add_test(NAME read_real COMMAND test_read "${_real}")
else()
  message(STATUS "real DAQ file not found; skipping read_real test")
endif()
