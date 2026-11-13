#include "amxxmodule.h"

#include "amxx/pawn_strings.hpp"
#include "core/archive_format.hpp"
#include "core/job_manager.hpp"
#include "core/logger.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using amxxarch::ArchiveEventType;
using amxxarch::ArchiveFormat;
using amxxarch::detect_format_from_file;
using amxxarch::jobs;
using amxxarch::parse_format;
using amxxarch::SubmitRequest;

namespace fs = std::filesystem;

constexpr int INVALID_PARAMS = -1;
constexpr int BAD_FORMAT = -2;
constexpr int UNKNOWN_FORMAT = -3;
constexpr int BAD_CALLBACK = -4;
constexpr int SHUTTING_DOWN = -5;
constexpr int FILE_NOT_FOUND = -6;

void console_error(const char* message) {
  std::string line = "[AmxxArch] ";
  line += message;
  line += "\n";
  SERVER_PRINT(line.c_str());
}

cell fail(cell code, std::string message) {
  amxxarch::log_error(message);
  return code;
}

fs::path resolve_relative_path(const fs::path& value) {
  const auto current = fs::current_path();
  auto first = value.begin();

  if (first != value.end() && *first == current.filename())
    return current.parent_path() / value;

  return current / value;
}

std::string absolute_path(std::string path) {
  fs::path value(path);
  if (value.is_relative()) value = resolve_relative_path(value);

  std::error_code error;
  if (fs::exists(value, error)) value = fs::weakly_canonical(value, error);

  return value.lexically_normal().string();
}

std::vector<char> make_pawn_string_array(const std::string& value) {
  std::vector<char> data(value.begin(), value.end());
  data.push_back('\0');
  return data;
}

cell native_extract(AMX* amx, cell* params) {
  const int argc = params[0] / sizeof(cell);
  if (argc < 3)
    return fail(INVALID_PARAMS, "arch_extract failed: invalid parameter count");

  std::string archive_path =
      absolute_path(amxxarch::pawn::get_string(amx, params[1], 0));
  std::string output_dir =
      absolute_path(amxxarch::pawn::get_string(amx, params[2], 1));
  std::string callback = amxxarch::pawn::get_string(amx, params[3], 2);
  std::string password;

  ArchiveFormat format = ArchiveFormat::AUTOMATIC;
  if (argc >= 4) {
    auto parsed = parse_format(static_cast<int>(params[4]));
    if (!parsed)
      return fail(BAD_FORMAT, "arch_extract failed: invalid format argument");
    format = *parsed;
  }

  if (argc >= 5) password = amxxarch::pawn::get_string(amx, params[5], 3);

  if (archive_path.empty() || output_dir.empty() || callback.empty()) {
    return fail(
        INVALID_PARAMS,
        "arch_extract failed: archive path, output dir or callback is empty");
  }

  std::error_code file_error;
  if (!fs::is_regular_file(archive_path, file_error)) {
    return fail(FILE_NOT_FOUND,
                "arch_extract failed: archive file not found: " + archive_path);
  }

  if (format == ArchiveFormat::AUTOMATIC)
    format = detect_format_from_file(archive_path);

  if (format == ArchiveFormat::UNKNOWN) {
    return fail(UNKNOWN_FORMAT,
                "arch_extract failed: unknown archive format: " + archive_path);
  }

  int forward =
      MF_RegisterSPForwardByName(amx, callback.c_str(), FP_CELL, FP_CELL,
                                 FP_ARRAY, FP_CELL, FP_CELL, FP_ARRAY, FP_DONE);

  if (forward < 0) {
    return fail(BAD_CALLBACK,
                "arch_extract failed: callback not found: " + callback);
  }

  int job = jobs().submit({std::move(archive_path), std::move(output_dir),
                           format, forward, std::move(password)});
  if (job <= 0) {
    MF_UnregisterSPForward(forward);
    return fail(SHUTTING_DOWN, "arch_extract failed: module is shutting down");
  }

  return job;
}

cell native_cancel(AMX*, cell* params) {
  if (params[0] / sizeof(cell) < 1) return 0;

  return jobs().cancel(static_cast<int>(params[1])) ? 1 : 0;
}

cell native_get_file_count(AMX*, cell* params) {
  if (params[0] / sizeof(cell) < 1) return 0;

  return static_cast<cell>(jobs().file_count(static_cast<int>(params[1])));
}

cell set_file_string(AMX* amx, cell* params, bool path) {
  if (params[0] / sizeof(cell) < 4) return 0;

  const int len = static_cast<int>(params[4]);
  if (len <= 0) return 0;

  std::string value;
  const int job_id = static_cast<int>(params[1]);
  const int index = static_cast<int>(params[2]);
  const bool found = path ? jobs().get_file_path(job_id, index, value)
                          : jobs().get_file_name(job_id, index, value);
  if (!found) return 0;

  MF_SetAmxString(amx, params[3], value.c_str(), len);
  return 1;
}

cell native_get_file_name(AMX* amx, cell* params) {
  return set_file_string(amx, params, false);
}

cell native_get_file_path(AMX* amx, cell* params) {
  return set_file_string(amx, params, true);
}

void dispatch_logs() { amxxarch::drain_log_messages(); }

void dispatch_callbacks() {
  for (auto event : jobs().drain_events(256)) {
    if (jobs().is_cancel_requested(event.job_id)) {
      if (!event.terminal) continue;

      event.type = ArchiveEventType::CANCELLED;
      event.entry.clear();
      event.processed = 0;
      event.total = 0;
      event.message = "cancelled";
    }

    auto entry = make_pawn_string_array(event.entry);
    auto message = make_pawn_string_array(event.message);

    const cell entry_ref = MF_PrepareCharArray(
        entry.data(), static_cast<unsigned int>(entry.size()));
    const cell message_ref = MF_PrepareCharArray(
        message.data(), static_cast<unsigned int>(message.size()));

    MF_ExecuteForward(event.forward_id, static_cast<cell>(event.job_id),
                      static_cast<cell>(event.type), entry_ref,
                      static_cast<cell>(event.processed),
                      static_cast<cell>(event.total), message_ref);

    if (event.terminal) {
      MF_UnregisterSPForward(event.forward_id);
      jobs().complete_job(event.job_id);
    }
  }
}

std::array<AMX_NATIVE_INFO, 6> NATIVES{{
    {"arch_extract", native_extract},
    {"arch_cancel", native_cancel},
    {"arch_get_file_count", native_get_file_count},
    {"arch_get_file_name", native_get_file_name},
    {"arch_get_file_path", native_get_file_path},
    {nullptr, nullptr},
}};

}  // namespace

void on_amxx_attach() {
  amxxarch::set_log_function(console_error);
  MF_AddNatives(NATIVES.data());
}

void on_start_frame() {
  dispatch_logs();
  dispatch_callbacks();
  dispatch_logs();
}

void on_amxx_detach() {
  for (int forward : amxxarch::jobs().shutdown())
    MF_UnregisterSPForward(forward);

  dispatch_logs();
  amxxarch::set_log_function(nullptr);
}
