#include "backend/extractor_backend.hpp"

#include "core/logger.hpp"

#include "CPP/Common/MyCom.h"
#include "CPP/Common/MyWindows.h"
#include "CPP/Windows/PropVariant.h"
#include "CPP/7zip/Archive/IArchive.h"
#include "CPP/7zip/IStream.h"
#include "CPP/7zip/IPassword.h"
#include "CPP/7zip/PropID.h"
#include "CPP/7zip/UI/Common/LoadCodecs.h"
#include "C/7zCrc.h"
#include "C/XzCrc64.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <codecvt>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <locale>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace NArchive::N7z {
void force_seven_z_registration();
}

namespace NArchive::NBz2 {
void force_bzip2_archive_registration();
}

namespace NArchive::NGz {
void force_gzip_registration();
}

namespace NArchive::NRar {
void force_rar_registration();
}

namespace NArchive::NRar5 {
void force_rar5_registration();
}

namespace NArchive::NTar {
void force_tar_registration();
}

namespace NArchive::NXz {
void force_xz_registration();
}

namespace NArchive::NZip {
void force_zip_registration();
}

namespace NCompress {
void force_copy_registration();
void force_rar_codecs_registration();

namespace NBZip2 {
void force_bzip2_registration();
}

namespace NBcj {
void force_bcj_registration();
}

namespace NBcj2 {
void force_bcj2_registration();
}

namespace NBranch {
void force_branch_registration();
}

namespace NDeflate {
void force_deflate_registration();
void force_deflate64_registration();
}  // namespace NDeflate

namespace NLzma {
void force_lzma_registration();
}

namespace NLzma2 {
void force_lzma2_registration();
}

namespace NPpmd {
void force_ppmd_registration();
}
}  // namespace NCompress

namespace NCrypto {
void force_my_aes_registration();

namespace N7z {
void force_7z_aes_registration();
}
}  // namespace NCrypto

void force_crc_registration();
void force_md5_registration();
void force_sha1_registration();
void force_sha256_registration();

namespace amxxarch {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view CANCELLED_MESSAGE = "cancelled";
constexpr UInt64 MAX_OPEN_CHECK_BYTES = 1ull << 23;

std::string ustring_to_utf8(const UString& value) {
  std::wstring wide;
  wide.reserve(value.Len());
  for (unsigned i = 0; i < value.Len(); ++i)
    wide.push_back(static_cast<wchar_t>(value[i]));

  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  return conv.to_bytes(wide);
}

UString ascii_to_ustring(std::string_view value) {
  UString out;
  for (char ch : value)
    out += static_cast<wchar_t>(static_cast<unsigned char>(ch));

  return out;
}

BSTR allocate_bstr(const UString& value) {
  return ::SysAllocStringLen(value.Ptr(), value.Len());
}

std::string path_key(const fs::path& path) {
  auto value = path.lexically_normal().generic_string();
  while (value.size() > 1 && value.back() == '/') value.pop_back();

#if defined(_WIN32)
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(tolower(ch)); });
#endif

  return value;
}

bool path_is_inside(const fs::path& root, const fs::path& path) {
  const auto root_key = path_key(root);
  const auto path_value = path_key(path);
  if (path_value == root_key) return true;

  return path_value.size() > root_key.size() &&
         path_value[root_key.size()] == '/' &&
         path_value.compare(0, root_key.size(), root_key) == 0;
}

fs::path safe_relative_path(std::string entry) {
  std::replace(entry.begin(), entry.end(), '\\', '/');

  const auto raw_path = fs::path(entry);
  if (raw_path.empty() || raw_path.has_root_name() ||
      raw_path.has_root_directory()) {
    throw std::runtime_error{"archive entry uses an absolute or empty path"};
  }

  auto rel = raw_path.lexically_normal();
  if (rel.empty() || rel == ".")
    throw std::runtime_error{"archive entry uses an empty path"};

  for (const auto& part : rel) {
    const auto value = part.generic_string();
    if (value.empty() || value == "." || value == ".." ||
        value.find(':') != std::string::npos)
      throw std::runtime_error{"archive entry attempts path traversal"};
  }

  return rel;
}

fs::path prepare_output_root(const fs::path& output_dir) {
  fs::create_directories(output_dir);
  return fs::weakly_canonical(output_dir);
}

void ensure_safe_directory(const fs::path& root, const fs::path& relative_dir) {
  auto current = root;

  for (const auto& part : relative_dir) {
    const auto value = part.generic_string();
    if (value.empty() || value == ".") continue;

    current /= part;

    std::error_code ec;
    const auto exists = fs::exists(current, ec);
    if (ec) {
      throw std::runtime_error{"failed to inspect output directory: " +
                               current.string()};
    }

    if (exists) {
      const auto resolved = fs::weakly_canonical(current, ec);
      if (ec || !path_is_inside(root, resolved)) {
        throw std::runtime_error{"output directory escapes extraction root: " +
                                 current.string()};
      }

      if (!fs::is_directory(resolved, ec) || ec) {
        throw std::runtime_error{"output path component is not a directory: " +
                                 current.string()};
      }

      continue;
    }

    fs::create_directory(current, ec);
    if (ec)
      throw std::runtime_error{"failed to create output directory: " +
                               current.string()};
  }
}

auto prepare_output_file(const fs::path& root, std::string entry) -> fs::path {
  const auto rel = safe_relative_path(std::move(entry));
  ensure_safe_directory(root, rel.parent_path());

  auto target = (root / rel).lexically_normal();
  if (!path_is_inside(root, target))
    throw std::runtime_error{"archive entry escapes extraction root"};

  std::error_code ec;
  if (fs::exists(target, ec)) {
    const auto resolved = fs::weakly_canonical(target, ec);
    if (ec || !path_is_inside(root, resolved)) {
      throw std::runtime_error{"output file escapes extraction root: " +
                               target.string()};
    }

    if (fs::is_directory(resolved, ec) && !ec) {
      throw std::runtime_error{"output file target is a directory: " +
                               target.string()};
    }
  }

  return target;
}

fs::path prepare_output_directory(const fs::path& root, std::string entry) {
  const auto rel = safe_relative_path(std::move(entry));
  ensure_safe_directory(root, rel);

  auto target = (root / rel).lexically_normal();
  if (!path_is_inside(root, target))
    throw std::runtime_error{"archive directory escapes extraction root"};

  return target;
}

std::string prop_path(IInArchive* archive, UInt32 index) {
  NWindows::NCOM::CPropVariant prop;
  if (archive->GetProperty(index, kpidPath, &prop) != S_OK ||
      prop.vt != VT_BSTR || prop.bstrVal == nullptr)
    return "item_" + std::to_string(index);

  auto value = ustring_to_utf8(UString{prop.bstrVal});
  if (value.empty()) value = "item_" + std::to_string(index);

  return value;
}

bool prop_is_dir(IInArchive* archive, UInt32 index) {
  NWindows::NCOM::CPropVariant prop;
  if (archive->GetProperty(index, kpidIsDir, &prop) != S_OK) return false;

  return prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE;
}

uint64_t prop_size(IInArchive* archive, UInt32 index) {
  NWindows::NCOM::CPropVariant prop;
  if (archive->GetProperty(index, kpidSize, &prop) != S_OK) return 0;

  if (prop.vt == VT_UI8) return static_cast<uint64_t>(prop.uhVal.QuadPart);
  if (prop.vt == VT_UI4) return static_cast<uint64_t>(prop.ulVal);

  return 0;
}

std::string_view operation_result_message(Int32 op_res) {
  switch (op_res) {
    case NArchive::NExtract::NOperationResult::kOK:
      return "ok";
    case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
      return "unsupported compression method";
    case NArchive::NExtract::NOperationResult::kDataError:
      return "data error";
    case NArchive::NExtract::NOperationResult::kCRCError:
      return "crc error";
    case NArchive::NExtract::NOperationResult::kUnavailable:
      return "data unavailable";
    case NArchive::NExtract::NOperationResult::kUnexpectedEnd:
      return "unexpected end of archive";
    case NArchive::NExtract::NOperationResult::kDataAfterEnd:
      return "data after end";
    case NArchive::NExtract::NOperationResult::kIsNotArc:
      return "not an archive";
    case NArchive::NExtract::NOperationResult::kHeadersError:
      return "headers error";
    case NArchive::NExtract::NOperationResult::kWrongPassword:
      return "wrong password";
    default:
      return "unknown extraction error";
  }
}

class StdInStream final : public IInStream, public CMyUnknownImp {
  Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
  Z7_IFACE_COM7_IMP(ISequentialInStream)
  Z7_IFACE_COM7_IMP(IInStream)

public:
  StdInStream(const fs::path& path) : file_{path, std::ios::binary} {
    if (!file_) {
      throw std::runtime_error{"cannot open archive for reading: " +
                               path.string()};
    }

    file_.seekg(0, std::ios::end);
    size_ = static_cast<uint64_t>(file_.tellg());
    file_.seekg(0, std::ios::beg);
  }

private:
  std::ifstream file_;
  uint64_t size_ = 0;
};

Z7_COM7F_IMF(StdInStream::Read(void* data, UInt32 size,
                               UInt32* processed_size)) {
  if (processed_size) *processed_size = 0;
  if (size == 0) return S_OK;

  file_.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
  const auto got =
      static_cast<UInt32>(std::max<std::streamsize>(0, file_.gcount()));
  if (processed_size) *processed_size = got;

  if (file_.bad()) return E_FAIL;
  return S_OK;
}

Z7_COM7F_IMF(StdInStream::Seek(Int64 offset, UInt32 seek_origin,
                               UInt64* new_position)) {
  std::ios_base::seekdir dir;
  switch (seek_origin) {
    case STREAM_SEEK_SET:
      dir = std::ios::beg;
      break;
    case STREAM_SEEK_CUR:
      dir = std::ios::cur;
      break;
    case STREAM_SEEK_END:
      dir = std::ios::end;
      break;
    default:
      return STG_E_INVALIDFUNCTION;
  }

  file_.clear();
  file_.seekg(static_cast<std::streamoff>(offset), dir);
  if (!file_) return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;

  if (new_position) *new_position = static_cast<UInt64>(file_.tellg());
  return S_OK;
}

class StdOutStream final : public ISequentialOutStream, public CMyUnknownImp {
  Z7_COM_UNKNOWN_IMP_1(ISequentialOutStream)
  Z7_IFACE_COM7_IMP(ISequentialOutStream)

public:
  StdOutStream(const fs::path& path)
      : file_{path, std::ios::binary | std::ios::trunc} {
    if (!file_)
      throw std::runtime_error{"cannot open output file: " + path.string()};
  }

private:
  std::ofstream file_;
};

Z7_COM7F_IMF(StdOutStream::Write(const void* data, UInt32 size,
                                 UInt32* processed_size)) {
  if (processed_size) *processed_size = 0;
  if (size == 0) return S_OK;

  file_.write(static_cast<const char*>(data),
              static_cast<std::streamsize>(size));
  if (!file_) return E_FAIL;

  if (processed_size) *processed_size = size;
  return S_OK;
}

class OpenCallback final : public IArchiveOpenCallback,
                           public ICryptoGetTextPassword,
                           public ICryptoGetTextPassword2,
                           public CMyUnknownImp {
  Z7_COM_UNKNOWN_IMP_3(IArchiveOpenCallback, ICryptoGetTextPassword,
                       ICryptoGetTextPassword2)
  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword2)

public:
  OpenCallback(std::string password)
      : password_defined_{!password.empty()},
        password_{ascii_to_ustring(password)} {}

private:
  bool password_defined_ = false;
  UString password_;
};

Z7_COM7F_IMF(OpenCallback::SetTotal(const UInt64*, const UInt64*)) {
  return S_OK;
}

Z7_COM7F_IMF(OpenCallback::SetCompleted(const UInt64*, const UInt64*)) {
  return S_OK;
}

Z7_COM7F_IMF(OpenCallback::CryptoGetTextPassword(BSTR* password)) {
  if (password == nullptr) return E_INVALIDARG;

  *password = nullptr;
  if (!password_defined_) return E_ABORT;

  *password = allocate_bstr(password_);
  return *password != nullptr ? S_OK : E_OUTOFMEMORY;
}

Z7_COM7F_IMF(OpenCallback::CryptoGetTextPassword2(Int32* password_is_defined,
                                                  BSTR* password)) {
  if (password_is_defined == nullptr || password == nullptr)
    return E_INVALIDARG;

  *password_is_defined = password_defined_ ? 1 : 0;
  *password = allocate_bstr(password_);
  return *password != nullptr ? S_OK : E_OUTOFMEMORY;
}

class ExtractCallback final : public IArchiveExtractCallback,
                              public ICryptoGetTextPassword,
                              public ICryptoGetTextPassword2,
                              public CMyUnknownImp {
  Z7_COM_UNKNOWN_IMP_4(IArchiveExtractCallback, IProgress,
                       ICryptoGetTextPassword, ICryptoGetTextPassword2)
  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IArchiveExtractCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword2)

public:
  ExtractCallback(IInArchive* archive, const ExtractionRequest& request,
                  const fs::path& output_root,
                  const std::atomic_bool& cancel_requested,
                  const EventSink& sink)
      : archive_{archive},
        request_{request},
        output_root_{output_root},
        cancel_requested_{cancel_requested},
        sink_{sink},
        password_defined_{!request.password.empty()},
        password_{ascii_to_ustring(request.password)} {}

  const std::vector<fs::path>& written_files() const noexcept {
    return written_files_;
  }

  const std::string& failed_message() const noexcept { return failed_message_; }

private:
  IInArchive* archive_ = nullptr;
  const ExtractionRequest& request_;
  fs::path output_root_;
  const std::atomic_bool& cancel_requested_;
  const EventSink& sink_;
  bool password_defined_ = false;
  UString password_;
  std::string current_entry_;
  std::string failed_message_;
  std::vector<fs::path> written_files_;
  uint64_t total_ = 0;
};

Z7_COM7F_IMF(ExtractCallback::SetTotal(UInt64 total)) {
  total_ = static_cast<uint64_t>(total);
  return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::SetCompleted(const UInt64* complete_value)) {
  if (cancel_requested_.load(std::memory_order_relaxed)) return E_ABORT;

  const auto done = complete_value ? static_cast<int64_t>(*complete_value) : 0;
  sink_(CallbackEvent{request_.job_id, 0, ArchiveEventType::PROGRESS,
                      current_entry_, done, static_cast<int64_t>(total_), "",
                      false});
  return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::GetStream(UInt32 index,
                                        ISequentialOutStream** out_stream,
                                        Int32 ask_extract_mode)) {
  *out_stream = nullptr;

  if (cancel_requested_.load(std::memory_order_relaxed)) return E_ABORT;

  current_entry_ = prop_path(archive_, index);

  if (ask_extract_mode != NArchive::NExtract::NAskMode::kExtract) return S_OK;

  if (prop_is_dir(archive_, index)) {
    prepare_output_directory(output_root_, current_entry_);
    sink_(CallbackEvent{request_.job_id, 0, ArchiveEventType::FILE,
                        current_entry_, 0, 0, "directory", false});
    return S_OK;
  }

  const auto target = prepare_output_file(output_root_, current_entry_);

  auto spec = new StdOutStream{target};
  CMyComPtr<ISequentialOutStream> stream = spec;
  *out_stream = stream.Detach();

  written_files_.push_back(target);

  CallbackEvent event{
      request_.job_id, 0,    ArchiveEventType::FILE,
      current_entry_,  0,    static_cast<int64_t>(prop_size(archive_, index)),
      "file",          false};
  event.output_path = target.string();
  sink_(std::move(event));
  return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::PrepareOperation(Int32)) {
  return cancel_requested_.load(std::memory_order_relaxed) ? E_ABORT : S_OK;
}

Z7_COM7F_IMF(ExtractCallback::SetOperationResult(Int32 op_res)) {
  if (op_res == NArchive::NExtract::NOperationResult::kOK) return S_OK;

  failed_message_ = std::string{"item failed"};
  if (!current_entry_.empty()) failed_message_ += ": " + current_entry_;
  failed_message_ += ": ";
  failed_message_ += operation_result_message(op_res);

  sink_(CallbackEvent{request_.job_id, 0, ArchiveEventType::FAILED,
                      current_entry_, 0, 0, failed_message_, false});
  return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::CryptoGetTextPassword(BSTR* password)) {
  if (password == nullptr) return E_INVALIDARG;

  *password = nullptr;
  if (!password_defined_) return E_ABORT;

  *password = allocate_bstr(password_);
  return *password != nullptr ? S_OK : E_OUTOFMEMORY;
}

Z7_COM7F_IMF(ExtractCallback::CryptoGetTextPassword2(Int32* password_is_defined,
                                                     BSTR* password)) {
  if (password_is_defined == nullptr || password == nullptr)
    return E_INVALIDARG;

  *password_is_defined = password_defined_ ? 1 : 0;
  *password = allocate_bstr(password_);
  return *password != nullptr ? S_OK : E_OUTOFMEMORY;
}

std::string_view extension_for_format(ArchiveFormat format) {
  switch (format) {
    case ArchiveFormat::TAR:
      return "tar";
    case ArchiveFormat::GZIP:
      return "gz";
    case ArchiveFormat::BZIP2:
      return "bz2";
    case ArchiveFormat::XZ:
      return "xz";
    case ArchiveFormat::TAR_GZIP:
      return "gz";
    case ArchiveFormat::TAR_BZIP2:
      return "bz2";
    case ArchiveFormat::TAR_XZ:
      return "xz";
    case ArchiveFormat::ZIP:
      return "zip";
    case ArchiveFormat::RAR:
      return "rar";
    case ArchiveFormat::Z7:
      return "7z";
    case ArchiveFormat::AUTOMATIC:
    case ArchiveFormat::UNKNOWN:
      break;
  }
  throw std::runtime_error{"unsupported archive format"};
}

bool has_rar5_marker(const fs::path& path) {
  static constexpr std::array<unsigned char, 8> RAR5_MARKER{
      'R', 'a', 'r', '!', 0x1a, 0x07, 0x01, 0x00};

  std::ifstream file{path, std::ios::binary};
  if (!file) return false;

  std::array<unsigned char, RAR5_MARKER.size()> marker{};
  file.read(reinterpret_cast<char*>(marker.data()),
            static_cast<std::streamsize>(marker.size()));

  return file.gcount() == static_cast<std::streamsize>(marker.size()) &&
         marker == RAR5_MARKER;
}

class CodecsHolder final {
public:
  CodecsHolder() {
    CrcGenerateTable();
    Crc64GenerateTable();

    force_crc_registration();
    force_md5_registration();
    force_sha1_registration();
    force_sha256_registration();

    NArchive::N7z::force_seven_z_registration();
    NArchive::NBz2::force_bzip2_archive_registration();
    NArchive::NGz::force_gzip_registration();
    NArchive::NRar::force_rar_registration();
    NArchive::NRar5::force_rar5_registration();
    NArchive::NTar::force_tar_registration();
    NArchive::NXz::force_xz_registration();
    NArchive::NZip::force_zip_registration();

    NCompress::force_copy_registration();
    NCompress::force_rar_codecs_registration();
    NCompress::NBZip2::force_bzip2_registration();
    NCompress::NBcj::force_bcj_registration();
    NCompress::NBcj2::force_bcj2_registration();
    NCompress::NBranch::force_branch_registration();
    NCompress::NDeflate::force_deflate_registration();
    NCompress::NDeflate::force_deflate64_registration();
    NCompress::NLzma::force_lzma_registration();
    NCompress::NLzma2::force_lzma2_registration();
    NCompress::NPpmd::force_ppmd_registration();

    NCrypto::force_my_aes_registration();
    NCrypto::N7z::force_7z_aes_registration();

    const auto hr = codecs_.Load();
    if (hr != S_OK) throw std::runtime_error{"codec registry failed to load"};
  }

  CCodecs& get() noexcept { return codecs_; }

private:
  CCodecs codecs_;
};

CodecsHolder& codecs() {
  static CodecsHolder holder;
  return holder;
}

std::vector<unsigned> format_indices_for_request(CCodecs& codecs,
                                                 ArchiveFormat format,
                                                 const fs::path& path) {
  std::vector<unsigned> indices;

  auto add_archive_type = [&](std::string_view type) {
    const auto index = codecs.FindFormatForArchiveType(ascii_to_ustring(type));
    if (index >= 0) indices.push_back(static_cast<unsigned>(index));
  };

  auto add_extension = [&](std::string_view ext) {
    const auto index = codecs.FindFormatForExtension(ascii_to_ustring(ext));
    if (index >= 0) indices.push_back(static_cast<unsigned>(index));
  };

  if (format == ArchiveFormat::RAR) {
    if (has_rar5_marker(path)) {
      add_archive_type("Rar5");
      add_archive_type("Rar");
    } else {
      add_archive_type("Rar");
      add_archive_type("Rar5");
    }

    return indices;
  }

  add_archive_type(format_name(format));
  if (indices.empty()) add_extension(extension_for_format(format));

  return indices;
}

CMyComPtr<IInArchive> open_archive(const ExtractionRequest& request,
                                   const fs::path& path, ArchiveFormat format) {
  auto& c = codecs().get();
  const auto format_indices = format_indices_for_request(c, format, path);
  if (format_indices.empty()) {
    throw std::runtime_error{"format is not registered: " +
                             std::string(format_name(format))};
  }

  HRESULT last_hr = S_FALSE;
  for (const auto format_index : format_indices) {
    CMyComPtr<IInArchive> archive;
    auto hr = c.CreateInArchive(format_index, archive);
    if (hr != S_OK || !archive)
      throw std::runtime_error{"failed to create archive handler"};

    auto stream_spec = new StdInStream{path};
    CMyComPtr<IInStream> stream = stream_spec;

    auto open_spec = new OpenCallback{request.password};
    CMyComPtr<IArchiveOpenCallback> open_cb = open_spec;

    UInt64 max_check = MAX_OPEN_CHECK_BYTES;
    hr = archive->Open(stream, &max_check, open_cb);

    if (hr == S_OK) return archive;
    if (hr == S_FALSE) {
      last_hr = hr;
      continue;
    }

    throw std::runtime_error{"open failed, hresult=" + std::to_string(hr)};
  }

  throw std::runtime_error{"could not open archive as " +
                           std::string(format_name(format)) +
                           ", hresult=" + std::to_string(last_hr)};
}

std::vector<fs::path> extract_one(const ExtractionRequest& request,
                                  const fs::path& archive_path,
                                  const fs::path& output_root,
                                  ArchiveFormat format,
                                  const std::atomic_bool& cancel_requested,
                                  const EventSink& sink) {
  auto archive = open_archive(request, archive_path, format);

  UInt32 count = 0;
  if (archive->GetNumberOfItems(&count) != S_OK)
    throw std::runtime_error{"failed to enumerate archive items"};

  auto cb_spec = new ExtractCallback{archive, request, output_root,
                                     cancel_requested, sink};
  CMyComPtr<IArchiveExtractCallback> cb = cb_spec;

  const auto hr = archive->Extract(nullptr, static_cast<UInt32>(-1), 0, cb);
  if (hr == E_ABORT || cancel_requested.load(std::memory_order_relaxed))
    throw std::runtime_error(std::string(CANCELLED_MESSAGE));

  if (hr != S_OK) {
    throw std::runtime_error{"extraction failed, hresult=" +
                             std::to_string(hr)};
  }

  if (!cb_spec->failed_message().empty())
    throw std::runtime_error{cb_spec->failed_message()};

  return cb_spec->written_files();
}

ArchiveFormat compressor_format_for_tar_combo(ArchiveFormat format) {
  switch (format) {
    case ArchiveFormat::TAR_GZIP:
      return ArchiveFormat::GZIP;
    case ArchiveFormat::TAR_BZIP2:
      return ArchiveFormat::BZIP2;
    case ArchiveFormat::TAR_XZ:
      return ArchiveFormat::XZ;
    default:
      return format;
  }
}

bool is_tar_combo(ArchiveFormat format) noexcept {
  return format == ArchiveFormat::TAR_GZIP ||
         format == ArchiveFormat::TAR_BZIP2 || format == ArchiveFormat::TAR_XZ;
}

class Backend7z final : public ExtractorBackend {
public:
  void extract(const ExtractionRequest& request,
               const std::atomic_bool& cancel_requested,
               const EventSink& sink) override {
    if (cancel_requested.load(std::memory_order_relaxed)) {
      sink(CallbackEvent{request.job_id,
                         0,
                         ArchiveEventType::CANCELLED,
                         {},
                         0,
                         0,
                         std::string(CANCELLED_MESSAGE),
                         true});
      return;
    }

    sink(CallbackEvent{request.job_id, 0, ArchiveEventType::STARTED,
                       request.archive_path.string(), 0, 0,
                       "extraction started", false});

    try {
      const auto output_root = prepare_output_root(request.output_dir);

      if (is_tar_combo(request.format)) {
        const auto tmp_dir =
            output_root / (".amxxarch_tmp_" + std::to_string(request.job_id));
        fs::remove_all(tmp_dir);
        fs::create_directories(tmp_dir);

        auto tmp_sink = [&sink](CallbackEvent event) {
          if (event.type == ArchiveEventType::FAILED) sink(std::move(event));
        };

        auto tmp_files =
            extract_one(request, request.archive_path, tmp_dir,
                        compressor_format_for_tar_combo(request.format),
                        cancel_requested, tmp_sink);
        const auto tar_it =
            std::find_if(tmp_files.begin(), tmp_files.end(),
                         [](const auto& p) { return fs::is_regular_file(p); });
        if (tar_it == tmp_files.end()) {
          throw std::runtime_error{
              "compressed tar stage did not produce a tar file"};
        }

        extract_one(request, *tar_it, output_root, ArchiveFormat::TAR,
                    cancel_requested, sink);
        fs::remove_all(tmp_dir);
      } else {
        extract_one(request, request.archive_path, output_root, request.format,
                    cancel_requested, sink);
      }

      if (cancel_requested.load(std::memory_order_relaxed)) {
        sink(CallbackEvent{request.job_id,
                           0,
                           ArchiveEventType::CANCELLED,
                           {},
                           0,
                           0,
                           std::string(CANCELLED_MESSAGE),
                           true});
      } else {
        sink(CallbackEvent{request.job_id, 0, ArchiveEventType::DONE,
                           request.archive_path.string(), 0, 0, "done", true});
      }
    } catch (const std::exception& ex) {
      if (std::string_view{ex.what()} == CANCELLED_MESSAGE) {
        sink(CallbackEvent{request.job_id,
                           0,
                           ArchiveEventType::CANCELLED,
                           {},
                           0,
                           0,
                           std::string(CANCELLED_MESSAGE),
                           true});
      } else {
        log_error("extraction failed: " + request.archive_path.string() + ": " +
                  ex.what());
        sink(CallbackEvent{request.job_id, 0, ArchiveEventType::FAILED,
                           request.archive_path.string(), 0, 0, ex.what(),
                           true});
      }
    }
  }
};

}  // namespace

std::unique_ptr<ExtractorBackend> make_extractor_backend() {
  return std::make_unique<Backend7z>();
}

}  // namespace amxxarch
