# dune-daq-hdf

A **pure** reader of the DUNE DAQ raw HDF5 layout, using the raw HDF5 C API (we
do not depend on DUNE `hdf5libs`) and the header-only `dune-daq-types`. It
navigates the file and yields **Fragments as raw bytes + identity** (record,
`SourceID`, `FragmentType`); it does **not** decode ADC payloads — that is
`dune-daq-codec`.

`DaqHdf5File(path)` → `records()` (top-level `<prefix><n>.<seq>` groups) →
`fragments(record)` (datasets under `RawData`, classified by the composite name
`<Subsystem>_0x<id>_<FragmentType>`) → `read_bytes(path)` + `FragmentView` over
the bytes. See `dune-daq-codec/docs/daq-hdf5-layout.md` for the layout.

No Arrow, no WCT, no DUNE libraries; reimplemented from the read-only
`reference/` clones. Errors throw `std::runtime_error`. libhdf5 is not
thread-safe — serialize access to a file.
