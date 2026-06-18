// Tests for DaqHdf5File (ddm-3j8.1.4).
//
// Primary: write a small SYNTHETIC DUNE-DAQ-shaped HDF5 file with the raw HDF5
// C API and read it back, asserting record/fragment enumeration, name parsing
// (SourceID + FragmentType), and byte/header decoding. Self-contained, no DUNE
// libs, no large data file.
//
// Optional: if argv[1] is a real DAQ file, run structural smoke checks on it.

#include "dune_daq_hdf/DaqHdf5File.hpp"

#include "dune_daq_types/FragmentHeader.hpp"

#include <hdf5.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace dune_daq_hdf;

static int fails = 0;
static void check(bool ok, const std::string& what)
{
    std::cout << (ok ? "ok   " : "FAIL ") << what << "\n";
    if (!ok) ++fails;
}

// --- synthetic file writer (raw HDF5) ---------------------------------------

template <typename T>
static void put(std::vector<std::byte>& b, std::size_t off, T v)
{
    std::memcpy(b.data() + off, &v, sizeof(T));  // little-endian host
}

// One fragment: 72-byte FragmentHeader + `payload` zero bytes.
static std::vector<std::byte> make_fragment(std::uint32_t type, std::uint16_t subsystem,
                                            std::uint32_t id, std::size_t payload)
{
    std::vector<std::byte> b(72 + payload, std::byte{0});
    put<std::uint32_t>(b, 0, 0x11112222);            // marker
    put<std::uint32_t>(b, 4, 5);                     // version
    put<std::uint64_t>(b, 8, b.size());              // size
    put<std::uint32_t>(b, 56, type);                 // fragment_type
    put<std::uint16_t>(b, 64, 2);                    // element_id.version
    put<std::uint16_t>(b, 66, subsystem);            // element_id.subsystem
    put<std::uint32_t>(b, 68, id);                   // element_id.id
    return b;
}

static void write_dataset(hid_t grp, const std::string& name, const std::vector<std::byte>& bytes)
{
    hsize_t dims[2] = {bytes.size(), 1};
    hid_t space = H5Screate_simple(2, dims, nullptr);
    hid_t dset = H5Dcreate2(grp, name.c_str(), H5T_STD_U8LE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(dset, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT, bytes.data());
    H5Dclose(dset);
    H5Sclose(space);
}

static void make_synthetic(const std::string& path)
{
    hid_t f = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    hid_t rec = H5Gcreate2(f, "TriggerRecord00007.0003", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    hid_t raw = H5Gcreate2(rec, "RawData", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    write_dataset(raw, "TR_Builder_0x00000001_TriggerRecordHeader", make_fragment(0, 4, 1, 4));
    write_dataset(raw, "Detector_Readout_0x00000190_WIBEth", make_fragment(12, 1, 0x190, 8));
    write_dataset(raw, "Detector_Readout_0x000000c9_TDEEth", make_fragment(15, 1, 0xc9, 16));
    H5Gclose(raw);
    H5Gclose(rec);
    H5Fclose(f);
}

// --- tests ------------------------------------------------------------------

static void test_synthetic()
{
    const std::string path = "/tmp/dune_daq_hdf_synthetic.h5";
    make_synthetic(path);
    DaqHdf5File file(path);

    auto recs = file.records();
    check(recs.size() == 1, "one record found");
    if (recs.empty()) return;
    check(recs[0].number == 7 && recs[0].sequence == 3, "record id parsed (7, 3)");

    auto frags = file.fragments(recs[0]);
    check(frags.size() == 2, "two fragments (header excluded)");

    bool saw_wibeth = false, saw_tdeeth = false;
    for (const auto& fi : frags) {
        check(fi.source_id.subsystem == dune_daq::SourceID::Subsystem::kDetectorReadout,
              "fragment subsystem = Detector_Readout");
        if (fi.type == dune_daq::FragmentType::kWIBEth) {
            saw_wibeth = true;
            check(fi.source_id.id == 0x190u, "WIBEth source id 0x190");
            check(fi.byte_size == 80, "WIBEth byte size == 72+8");
            auto bytes = file.read_bytes(fi.dataset_path);
            FragmentView v{bytes.data(), bytes.size()};
            check(v.header().valid_marker(), "WIBEth marker valid");
            check(v.header().type() == dune_daq::FragmentType::kWIBEth, "WIBEth header type");
            check(v.header().size == 80 && v.payload_size() == 8, "WIBEth header size/payload");
        }
        if (fi.type == dune_daq::FragmentType::kTDEEth) {
            saw_tdeeth = true;
            check(fi.source_id.id == 0xc9u, "TDEEth source id 0xc9");
        }
    }
    check(saw_wibeth && saw_tdeeth, "both WIBEth and TDEEth classified");

    auto hdr = file.record_header(recs[0]);
    check(hdr.has_value(), "record header found");
    if (hdr) check(hdr->dataset_path.find("TriggerRecordHeader") != std::string::npos,
                   "record header is the TriggerRecordHeader dataset");
}

static void test_real(const std::string& path)
{
    std::cout << "## real file: " << path << "\n";
    DaqHdf5File file(path);
    auto recs = file.records();
    check(!recs.empty(), "real: >=1 record");
    if (recs.empty()) return;
    auto frags = file.fragments(recs[0]);
    check(!frags.empty(), "real: >=1 fragment in first record");
    int checked = 0;
    for (const auto& fi : frags) {
        if (fi.type != dune_daq::FragmentType::kWIBEth) continue;
        auto bytes = file.read_bytes(fi.dataset_path);
        FragmentView v{bytes.data(), bytes.size()};
        check(v.header().valid_marker(), "real: WIBEth marker 0x11112222");
        // The DATASET byte length is authoritative and frame-aligned; the
        // FragmentHeader.size FIELD is unreliable in real data (sometimes a
        // round buffer size, sometimes < the actual content) so we do NOT
        // require header.size == dataset bytes. payload_size() uses the dataset
        // length (FragmentView::size), not the header field.
        check(v.payload_size() % 7200 == 0, "real: dataset payload is N x 7200 (frame-aligned)");
        if (++checked == 1) break;  // one is enough; avoid reading many 1 MB fragments
    }
    check(checked == 1, "real: found and validated a WIBEth fragment");
}

int main(int argc, char** argv)
{
    test_synthetic();
    if (argc > 1) test_real(argv[1]);
    if (fails) { std::cerr << fails << " failures\n"; return 1; }
    std::cout << "dune-daq-hdf OK\n";
    return 0;
}
