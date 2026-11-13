#include "core/archive_format.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace amxxarch {
namespace {

struct FormatExtension {
  std::string_view extension;
  ArchiveFormat format;
};

struct FormatName {
  ArchiveFormat format;
  std::string_view name;
};

constexpr std::array FORMAT_EXTENSIONS = {
    FormatExtension{".tar.bz2", ArchiveFormat::TAR_BZIP2},
    FormatExtension{".tar.gz", ArchiveFormat::TAR_GZIP},
    FormatExtension{".tar.xz", ArchiveFormat::TAR_XZ},
    FormatExtension{".bzip2", ArchiveFormat::BZIP2},
    FormatExtension{".gzip", ArchiveFormat::GZIP},
    FormatExtension{".tbz2", ArchiveFormat::TAR_BZIP2},
    FormatExtension{".tar", ArchiveFormat::TAR},
    FormatExtension{".tgz", ArchiveFormat::TAR_GZIP},
    FormatExtension{".tbz", ArchiveFormat::TAR_BZIP2},
    FormatExtension{".txz", ArchiveFormat::TAR_XZ},
    FormatExtension{".bz2", ArchiveFormat::BZIP2},
    FormatExtension{".gz", ArchiveFormat::GZIP},
    FormatExtension{".xz", ArchiveFormat::XZ},
    FormatExtension{".zip", ArchiveFormat::ZIP},
    FormatExtension{".rar", ArchiveFormat::RAR},
    FormatExtension{".7z", ArchiveFormat::Z7}};

constexpr std::array FORMAT_NAMES = {
    FormatName{ArchiveFormat::AUTOMATIC, "auto"},
    FormatName{ArchiveFormat::TAR, "tar"},
    FormatName{ArchiveFormat::GZIP, "gzip"},
    FormatName{ArchiveFormat::BZIP2, "bzip2"},
    FormatName{ArchiveFormat::XZ, "xz"},
    FormatName{ArchiveFormat::TAR_GZIP, "tar.gz"},
    FormatName{ArchiveFormat::TAR_BZIP2, "tar.bz2"},
    FormatName{ArchiveFormat::TAR_XZ, "tar.xz"},
    FormatName{ArchiveFormat::ZIP, "zip"},
    FormatName{ArchiveFormat::RAR, "rar"},
    FormatName{ArchiveFormat::Z7, "7z"},
    FormatName{ArchiveFormat::UNKNOWN, "unknown"}};

std::string lowercase(std::string_view text) {
  std::string result;
  result.reserve(text.size());

  for (unsigned char ch : text)
    result.push_back(static_cast<char>(tolower(ch)));

  return result;
}

bool has_suffix(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool has_prefix(const std::array<unsigned char, 512>& header, std::size_t size,
                std::initializer_list<unsigned char> signature) {
  if (size < signature.size()) return false;

  std::size_t i = 0;
  for (auto byte : signature) {
    if (header[i++] != byte) return false;
  }

  return true;
}

bool has_text_at(const std::array<unsigned char, 512>& header, std::size_t size,
                 std::size_t offset, std::string_view text) {
  if (size < offset + text.size()) return false;

  for (std::size_t i = 0; i < text.size(); ++i) {
    if (header[offset + i] != static_cast<unsigned char>(text[i])) return false;
  }

  return true;
}

ArchiveFormat format_for_compressed_magic(
    ArchiveFormat by_name, ArchiveFormat compressed_format) noexcept {
  switch (compressed_format) {
    case ArchiveFormat::GZIP:
      return by_name == ArchiveFormat::TAR_GZIP ? ArchiveFormat::TAR_GZIP
                                                : ArchiveFormat::GZIP;

    case ArchiveFormat::BZIP2:
      return by_name == ArchiveFormat::TAR_BZIP2 ? ArchiveFormat::TAR_BZIP2
                                                 : ArchiveFormat::BZIP2;

    case ArchiveFormat::XZ:
      return by_name == ArchiveFormat::TAR_XZ ? ArchiveFormat::TAR_XZ
                                              : ArchiveFormat::XZ;

    default:
      return compressed_format;
  }
}

}  // namespace

std::array<ArchiveFormat, 10> supported_formats() noexcept {
  return {
      ArchiveFormat::TAR,    ArchiveFormat::GZIP,     ArchiveFormat::BZIP2,
      ArchiveFormat::XZ,     ArchiveFormat::TAR_GZIP, ArchiveFormat::TAR_BZIP2,
      ArchiveFormat::TAR_XZ, ArchiveFormat::ZIP,      ArchiveFormat::RAR,
      ArchiveFormat::Z7,
  };
}

std::optional<ArchiveFormat> parse_format(int value) {
  auto format = static_cast<ArchiveFormat>(value);
  if (format == ArchiveFormat::AUTOMATIC) return format;

  auto formats = supported_formats();
  if (std::find(formats.begin(), formats.end(), format) == formats.end())
    return std::nullopt;

  return format;
}

ArchiveFormat detect_format_from_name(std::string_view path) {
  std::string name = lowercase(path);

  for (const auto& item : FORMAT_EXTENSIONS) {
    if (has_suffix(name, item.extension)) return item.format;
  }

  return ArchiveFormat::UNKNOWN;
}

ArchiveFormat detect_format_from_file(const std::filesystem::path& path) {
  const auto by_name = detect_format_from_name(path.generic_string());

  std::ifstream file{path, std::ios::binary};
  if (!file) return by_name;

  std::array<unsigned char, 512> header{};
  file.read(reinterpret_cast<char*>(header.data()),
            static_cast<std::streamsize>(header.size()));

  const auto size =
      static_cast<std::size_t>(std::max<std::streamsize>(0, file.gcount()));

  if (has_prefix(header, size, {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c}))
    return ArchiveFormat::Z7;

  if (has_prefix(header, size, {0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00}))
    return format_for_compressed_magic(by_name, ArchiveFormat::XZ);

  if (has_prefix(header, size, {0x1f, 0x8b}))
    return format_for_compressed_magic(by_name, ArchiveFormat::GZIP);

  if (has_prefix(header, size, {'B', 'Z', 'h'}))
    return format_for_compressed_magic(by_name, ArchiveFormat::BZIP2);

  if (has_prefix(header, size, {'R', 'a', 'r', '!', 0x1a, 0x07, 0x00}))
    return ArchiveFormat::RAR;

  if (has_prefix(header, size, {'R', 'a', 'r', '!', 0x1a, 0x07, 0x01, 0x00}))
    return ArchiveFormat::RAR;

  if (has_prefix(header, size, {'P', 'K', 0x03, 0x04}) ||
      has_prefix(header, size, {'P', 'K', 0x05, 0x06}) ||
      has_prefix(header, size, {'P', 'K', 0x06, 0x06}) ||
      has_prefix(header, size, {'P', 'K', 0x07, 0x08, 'P', 'K'}))
    return ArchiveFormat::ZIP;

  if (has_text_at(header, size, 257, "ustar")) return ArchiveFormat::TAR;

  return by_name;
}

std::string_view format_name(ArchiveFormat format) noexcept {
  for (const auto& item : FORMAT_NAMES) {
    if (item.format == format) return item.name;
  }

  return "unknown";
}

}  // namespace amxxarch
