#ifndef DUNE_DAQ_HDF_DAQHDF5FILE_HPP
#define DUNE_DAQ_HDF_DAQHDF5FILE_HPP

// Read a DUNE DAQ raw HDF5 file (ddm-3j8.1.4).
//
// A pure reader of the DUNE DAQ HDF5 layout using the raw HDF5 C API (we do NOT
// depend on DUNE hdf5libs; the layout is documented in
// dune-daq-codec/docs/daq-hdf5-layout.md). It navigates the file and hands out
// Fragments as raw bytes + identity (record, SourceID, FragmentType); it does
// NOT decode ADC payloads (that is dune-daq-codec).
//
// Strategy: traverse + classify by dataset name. Records are top-level groups
// `<prefix><number>.<sequence>`; under each record's `RawData` group the
// datasets are flat, composite-named `<Subsystem>_0x<id_hex>_<FragmentType>`.
//
// Depends only on HDF5 (C) and the header-only dune-daq-types. No Arrow, no WCT,
// no DUNE libraries. Errors throw std::runtime_error. libhdf5 is not
// thread-safe; serialize access to one file.

#include "dune_daq_types/FragmentHeader.hpp"
#include "dune_daq_types/FragmentType.hpp"
#include "dune_daq_types/SourceID.hpp"

#include <hdf5.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dune_daq_hdf {

/// Identifies a record (TriggerRecord / TimeSlice) within the file.
struct RecordID {
    std::uint64_t number{0};
    std::uint32_t sequence{0};
    std::string group;  ///< the HDF5 group name (e.g. "TriggerRecord01663.0000")
    friend bool operator==(const RecordID&, const RecordID&) = default;
};

/// One dataset within a record's RawData group, with parsed identity.
struct FragmentInfo {
    dune_daq::SourceID source_id{};
    dune_daq::FragmentType type{dune_daq::FragmentType::kUnknown};
    std::string dataset_path;   ///< full HDF5 path to the dataset
    std::uint64_t byte_size{0}; ///< dataset length in bytes (== FragmentHeader.size)
};

/// Non-owning view over a fragment's raw bytes.
///
/// `size` is the DATASET byte length and is AUTHORITATIVE — in real DAQ data it
/// is consistently frame-aligned (`sizeof(FragmentHeader) + N × frame_bytes`).
/// The in-payload `header().size` FIELD is NOT reliable (observed as a round
/// buffer size, and sometimes smaller than the actual content), so always size
/// the payload from `FragmentView::size`, never from `header().size`.
struct FragmentView {
    const std::byte* data{nullptr};
    std::size_t size{0};

    const dune_daq::FragmentHeader& header() const
    {
        return *reinterpret_cast<const dune_daq::FragmentHeader*>(data);
    }
    const std::byte* payload() const { return data + sizeof(dune_daq::FragmentHeader); }
    std::size_t payload_size() const { return size - sizeof(dune_daq::FragmentHeader); }
};

class DaqHdf5File {
  public:
    /// Open a DUNE DAQ HDF5 file read-only. Throws std::runtime_error on failure.
    explicit DaqHdf5File(const std::string& path);
    ~DaqHdf5File();
    DaqHdf5File(DaqHdf5File&&) noexcept;
    DaqHdf5File& operator=(DaqHdf5File&&) noexcept;
    DaqHdf5File(const DaqHdf5File&) = delete;
    DaqHdf5File& operator=(const DaqHdf5File&) = delete;

    /// All records in the file (top-level `<prefix><n>.<seq>` groups), in file order.
    std::vector<RecordID> records() const;

    /// The detector/trigger/HSI Fragments under a record's RawData group
    /// (EXCLUDING the record header). Parsed identity from the dataset name.
    std::vector<FragmentInfo> fragments(const RecordID& rid) const;

    /// The record-header dataset (FragmentType kUnknown, name "…TriggerRecordHeader"),
    /// if present.
    std::optional<FragmentInfo> record_header(const RecordID& rid) const;

    /// Read a dataset's raw bytes (e.g. FragmentInfo::dataset_path).
    std::vector<std::byte> read_bytes(const std::string& dataset_path) const;

  private:
    hid_t m_file{H5I_INVALID_HID};
};

}  // namespace dune_daq_hdf

#endif  // DUNE_DAQ_HDF_DAQHDF5FILE_HPP
