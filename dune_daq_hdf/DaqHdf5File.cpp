#include "dune_daq_hdf/DaqHdf5File.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace dune_daq_hdf {

namespace {

// RAII for HDF5 ids (mirrors arrow-hdf's Hid).
struct Hid {
    hid_t id{H5I_INVALID_HID};
    herr_t (*closer)(hid_t){nullptr};
    Hid() = default;
    Hid(hid_t i, herr_t (*c)(hid_t)) : id(i), closer(c) {}
    Hid(Hid&& o) noexcept : id(o.id), closer(o.closer) { o.id = H5I_INVALID_HID; }
    Hid& operator=(Hid&&) = delete;
    Hid(const Hid&) = delete;
    ~Hid() { if (id >= 0 && closer) closer(id); }
    operator hid_t() const { return id; }
    bool ok() const { return id >= 0; }
};

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error("dune-daq-hdf: " + msg); }

// Collect the names of a group's direct child links.
struct Names { std::vector<std::string> names; };
herr_t collect_cb(hid_t, const char* name, const H5L_info2_t*, void* op)
{
    static_cast<Names*>(op)->names.emplace_back(name);
    return 0;
}
std::vector<std::string> child_names(hid_t loc, const std::string& path)
{
    Hid g(H5Gopen2(loc, path.c_str(), H5P_DEFAULT), H5Gclose);
    if (!g.ok()) fail("open group " + path);
    Names n;
    if (H5Literate2(g, H5_INDEX_NAME, H5_ITER_NATIVE, nullptr, collect_cb, &n) < 0) {
        fail("iterate group " + path);
    }
    return std::move(n.names);
}

bool all_digits(std::string_view s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// "<prefix><number>.<sequence>" (e.g. "TriggerRecord01663.0000"), or
// "<prefix><number>" (no sequence). Returns nullopt if it does not parse.
std::optional<RecordID> parse_record_name(const std::string& name)
{
    std::string head = name;
    std::uint32_t seq = 0;
    if (auto dot = name.rfind('.'); dot != std::string::npos) {
        const std::string s = name.substr(dot + 1);
        if (!all_digits(s)) return std::nullopt;
        seq = static_cast<std::uint32_t>(std::stoul(s));
        head = name.substr(0, dot);
    }
    // trailing run of digits in `head` is the record number
    std::size_t i = head.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(head[i - 1]))) --i;
    if (i == head.size()) return std::nullopt;  // no trailing digits -> not a record
    RecordID rid;
    rid.number = std::stoull(head.substr(i));
    rid.sequence = seq;
    rid.group = name;
    return rid;
}

dune_daq::SourceID::Subsystem subsystem_from_name(std::string_view s)
{
    using S = dune_daq::SourceID::Subsystem;
    if (s == "Detector_Readout") return S::kDetectorReadout;
    if (s == "HW_Signals_Interface") return S::kHwSignalsInterface;
    if (s == "Trigger") return S::kTrigger;
    if (s == "TR_Builder") return S::kTRBuilder;
    return S::kUnknown;
}

// Parsed pieces of a "<Subsystem>_0x<hex>_<FragmentType>" dataset name.
struct ParsedName {
    dune_daq::SourceID source_id;
    std::string frag_type_name;  // the trailing FragmentType name (e.g. "WIBEth")
};
std::optional<ParsedName> parse_fragment_name(const std::string& name)
{
    const auto x = name.find("_0x");
    if (x == std::string::npos) return std::nullopt;
    const std::string subsys = name.substr(0, x);
    const std::size_t hex_begin = x + 3;  // past "_0x"
    std::size_t hex_end = hex_begin;
    while (hex_end < name.size() && std::isxdigit(static_cast<unsigned char>(name[hex_end]))) ++hex_end;
    if (hex_end == hex_begin || hex_end >= name.size() || name[hex_end] != '_') return std::nullopt;
    ParsedName p;
    p.source_id.version = dune_daq::SourceID::s_source_id_version;
    p.source_id.subsystem = subsystem_from_name(subsys);
    p.source_id.id = static_cast<dune_daq::SourceID::ID_t>(
        std::stoul(name.substr(hex_begin, hex_end - hex_begin), nullptr, 16));
    p.frag_type_name = name.substr(hex_end + 1);
    return p;
}

constexpr const char* kRawData = "RawData";
constexpr const char* kRecordHeaderSuffix = "TriggerRecordHeader";

std::uint64_t dataset_bytes(hid_t loc, const std::string& path)
{
    Hid dset(H5Dopen2(loc, path.c_str(), H5P_DEFAULT), H5Dclose);
    if (!dset.ok()) fail("open dataset " + path);
    Hid space(H5Dget_space(dset), H5Sclose);
    const int nd = H5Sget_simple_extent_ndims(space);
    std::vector<hsize_t> dims(nd > 0 ? nd : 1, 1);
    if (nd > 0) H5Sget_simple_extent_dims(space, dims.data(), nullptr);
    std::uint64_t n = 1;
    for (int d = 0; d < nd; ++d) n *= dims[d];
    return n;
}

// Build the FragmentInfo list for a record, optionally returning only the
// record header (want_header=true) or only the non-header fragments.
std::vector<FragmentInfo> scan_record(hid_t file, const RecordID& rid, bool want_header)
{
    const std::string raw = "/" + rid.group + "/" + kRawData;
    std::vector<FragmentInfo> out;
    for (const auto& dsname : child_names(file, raw)) {
        auto p = parse_fragment_name(dsname);
        if (!p) continue;
        const bool is_header = (p->frag_type_name == kRecordHeaderSuffix);
        if (is_header != want_header) continue;
        FragmentInfo fi;
        fi.source_id = p->source_id;
        fi.type = is_header
                    ? dune_daq::FragmentType::kUnknown
                    : dune_daq::fragment_type_from_string(p->frag_type_name)
                        .value_or(dune_daq::FragmentType::kUnknown);
        fi.dataset_path = raw + "/" + dsname;
        fi.byte_size = dataset_bytes(file, fi.dataset_path);
        out.push_back(std::move(fi));
    }
    return out;
}

}  // namespace

DaqHdf5File::DaqHdf5File(const std::string& path)
{
    m_file = H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (m_file < 0) fail("H5Fopen(ro) " + path);
}

DaqHdf5File::~DaqHdf5File()
{
    if (m_file >= 0) H5Fclose(m_file);
}

DaqHdf5File::DaqHdf5File(DaqHdf5File&& o) noexcept : m_file(o.m_file) { o.m_file = H5I_INVALID_HID; }
DaqHdf5File& DaqHdf5File::operator=(DaqHdf5File&& o) noexcept
{
    if (this != &o) {
        if (m_file >= 0) H5Fclose(m_file);
        m_file = o.m_file;
        o.m_file = H5I_INVALID_HID;
    }
    return *this;
}

std::vector<RecordID> DaqHdf5File::records() const
{
    std::vector<RecordID> out;
    for (const auto& name : child_names(m_file, "/")) {
        if (auto rid = parse_record_name(name)) out.push_back(std::move(*rid));
    }
    return out;
}

std::vector<FragmentInfo> DaqHdf5File::fragments(const RecordID& rid) const
{
    return scan_record(m_file, rid, /*want_header=*/false);
}

std::optional<FragmentInfo> DaqHdf5File::record_header(const RecordID& rid) const
{
    auto v = scan_record(m_file, rid, /*want_header=*/true);
    if (v.empty()) return std::nullopt;
    return v.front();
}

std::vector<std::byte> DaqHdf5File::read_bytes(const std::string& dataset_path) const
{
    Hid dset(H5Dopen2(m_file, dataset_path.c_str(), H5P_DEFAULT), H5Dclose);
    if (!dset.ok()) fail("open dataset " + dataset_path);
    const std::uint64_t n = dataset_bytes(m_file, dataset_path);
    std::vector<std::byte> buf(n);
    if (n && H5Dread(dset, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data()) < 0) {
        fail("H5Dread " + dataset_path);
    }
    return buf;
}

}  // namespace dune_daq_hdf
