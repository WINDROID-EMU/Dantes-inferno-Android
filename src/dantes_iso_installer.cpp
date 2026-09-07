#include "dantes_iso_installer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <rex/logging.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <commdlg.h>
#include <windows.h>
#elif defined(__APPLE__)
#elif defined(__ANDROID__)
#include <android/log.h>
#else
#endif

namespace dantes {

#if defined(__ANDROID__)
namespace android {
static std::mutex g_iso_picker_mutex;
static std::condition_variable g_iso_picker_cv;
static std::string g_pending_iso_path;
static bool g_iso_picker_done = false;

void SetPendingIsoPath(const std::string& path) {
  std::unique_lock<std::mutex> lock(g_iso_picker_mutex);
  g_pending_iso_path = path;
  g_iso_picker_done = true;
  g_iso_picker_cv.notify_all();
}

std::filesystem::path ConsumePendingIsoPath() {
  std::unique_lock<std::mutex> lock(g_iso_picker_mutex);
  if (!g_iso_picker_done && g_pending_iso_path.empty()) {
    // Check environment variable as fallback
    if (const char* env_iso = std::getenv("DANTES_INSTALL_ISO")) {
      if (*env_iso != '\0') {
        return env_iso;
      }
    }
    // Wait up to 5 minutes if not already set
    g_iso_picker_cv.wait_for(lock, std::chrono::minutes(5), [] { return g_iso_picker_done; });
  }
  return g_pending_iso_path;
}
}  // namespace android
#endif

namespace {

// ----------------------------------------------------------------------------
// Minimal read-only XDVDFS (GDF) reader — the filesystem used by Xbox and
// Xbox 360 game discs. Directory tables are AVL trees of 4-byte-aligned
// entries; files are stored as contiguous sector runs.
// ----------------------------------------------------------------------------

constexpr uint32_t kSectorSize = 2048;
constexpr std::string_view kVolumeMagic = "MICROSOFT*XBOX*MEDIA";
// The volume descriptor lives at sector 32 of the game partition. Redump-style
// Xbox 360 images place the game partition at 0xFD90000 (XGD2) or 0x2080000
// (XGD3); a bare partition dump has it at 0.
constexpr std::array<uint64_t, 3> kPartitionBases = {0x0ull, 0xFD90000ull, 0x2080000ull};
constexpr uint8_t kAttributeDirectory = 0x10;
constexpr uint16_t kEmptyDirectorySentinel = 0xFFFF;
constexpr uint32_t kMaxDirectoryDepth = 32;

uint16_t Le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t Le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct DiscFileEntry {
  std::filesystem::path relative_path;
  uint32_t start_sector = 0;
  uint32_t size = 0;
};

class XdvdfsImageReader {
 public:
  bool Open(const std::filesystem::path& iso_path, std::string& error) {
    file_.open(iso_path, std::ios::binary);
    if (!file_) {
      error = "Unable to open " + iso_path.string() + ".";
      return false;
    }
    std::array<char, kVolumeMagic.size()> magic{};
    for (const uint64_t base : kPartitionBases) {
      file_.clear();
      file_.seekg(static_cast<std::streamoff>(base + 32ull * kSectorSize));
      if (!file_.read(magic.data(), magic.size())) {
        continue;
      }
      if (std::string_view(magic.data(), magic.size()) == kVolumeMagic) {
        partition_base_ = base;
        return true;
      }
    }
    error =
        "The selected file is not an Xbox 360 disc image (no XDVDFS game "
        "partition found). Select a full-disc .iso dumped from Dante's Inferno.";
    return false;
  }

  bool ListFiles(std::vector<DiscFileEntry>& files, std::string& error) {
    files.clear();
    std::array<uint8_t, 8> descriptor{};
    file_.clear();
    file_.seekg(
        static_cast<std::streamoff>(partition_base_ + 32ull * kSectorSize + kVolumeMagic.size()));
    if (!file_.read(reinterpret_cast<char*>(descriptor.data()), descriptor.size())) {
      error = "The disc image's volume descriptor is truncated.";
      return false;
    }
    const uint32_t root_sector = Le32(descriptor.data());
    const uint32_t root_size = Le32(descriptor.data() + 4);
    return WalkDirectory(root_sector, root_size, {}, 0, files, error);
  }

  bool HasXex2Magic(const DiscFileEntry& entry, std::string& error) {
    std::array<char, 4> magic{};
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(partition_base_ +
                                            static_cast<uint64_t>(entry.start_sector) *
                                                kSectorSize));
    if (!file_.read(magic.data(), magic.size())) {
      error = "The disc image's default.xex is truncated.";
      return false;
    }
    if (std::string_view(magic.data(), magic.size()) != "XEX2") {
      error = "The disc image's default.xex is corrupt (missing XEX2 header).";
      return false;
    }
    return true;
  }

  bool ExtractFile(const DiscFileEntry& entry, const std::filesystem::path& destination,
                   std::atomic<uint64_t>* copied_bytes, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
      error = "Unable to create " + destination.parent_path().string() + ".";
      return false;
    }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "Unable to create " + destination.string() + ".";
      return false;
    }
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(partition_base_ +
                                            static_cast<uint64_t>(entry.start_sector) *
                                                kSectorSize));
    std::vector<char> buffer(4 * 1024 * 1024);
    uint64_t remaining = entry.size;
    while (remaining > 0) {
      const auto chunk =
          static_cast<std::streamsize>(std::min<uint64_t>(buffer.size(), remaining));
      if (!file_.read(buffer.data(), chunk)) {
        error = "Unexpected end of disc image while extracting " +
                entry.relative_path.string() + ".";
        return false;
      }
      out.write(buffer.data(), chunk);
      remaining -= static_cast<uint64_t>(chunk);
      if (copied_bytes) {
        copied_bytes->fetch_add(static_cast<uint64_t>(chunk), std::memory_order_relaxed);
      }
    }
    out.flush();
    if (!out) {
      error = "Failed to write " + destination.string() + ".";
      return false;
    }
    return true;
  }

 private:
  bool WalkDirectory(uint32_t table_sector, uint32_t table_size,
                     const std::filesystem::path& relative_dir,
                     uint32_t depth,
                     std::vector<DiscFileEntry>& files, std::string& error) {
    if (depth > kMaxDirectoryDepth) {
      error = "The disc image's directory tree exceeds the supported depth of " +
              std::to_string(kMaxDirectoryDepth) + ".";
      REXLOG_ERROR("{}", error);
      return false;
    }
    if (table_size == 0) {
      return true;  // Empty directory.
    }
    std::vector<uint8_t> table(table_size);
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(partition_base_ +
                                            static_cast<uint64_t>(table_sector) * kSectorSize));
    if (!file_.read(reinterpret_cast<char*>(table.data()), table.size())) {
      error = "The disc image's directory table for '" + relative_dir.string() +
              "' is truncated.";
      return false;
    }
    // Iterative AVL walk; the visited set guards against malformed images with
    // cyclic child offsets.
    std::vector<uint32_t> pending{0};
    std::unordered_set<uint32_t> visited;
    while (!pending.empty()) {
      const uint32_t dword_offset = pending.back();
      pending.pop_back();
      if (!visited.insert(dword_offset).second) {
        continue;
      }
      const uint64_t offset = static_cast<uint64_t>(dword_offset) * 4;
      if (offset + 14 > table.size()) {
        continue;
      }
      const uint16_t left = Le16(table.data() + offset);
      const uint16_t right = Le16(table.data() + offset + 2);
      if (left == kEmptyDirectorySentinel) {
        continue;
      }
      const uint32_t start_sector = Le32(table.data() + offset + 4);
      const uint32_t size = Le32(table.data() + offset + 8);
      const uint8_t attributes = table[offset + 12];
      const uint8_t name_length = table[offset + 13];
      if (offset + 14 + name_length <= table.size()) {
        const std::string name(reinterpret_cast<const char*>(table.data() + offset + 14),
                               name_length);
        const std::filesystem::path name_path(name);
        if (name.empty() || name == "." || name == ".." ||
            name.find('\0') != std::string::npos || name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos || name_path.is_absolute() ||
            name_path.has_root_name() || name_path.has_root_directory()) {
          error = "The disc image contains an unsafe directory entry name.";
          REXLOG_ERROR("Rejecting unsafe XDVDFS entry name '{}' under '{}'.", name,
                       relative_dir.string());
          return false;
        }
        const auto child_path = relative_dir / name_path;
        if (attributes & kAttributeDirectory) {
          if (!WalkDirectory(start_sector, size, child_path, depth + 1, files,
                             error)) {
            return false;
          }
        } else {
          files.push_back({child_path, start_sector, size});
        }
      }
      if (left != 0 && left != kEmptyDirectorySentinel) {
        pending.push_back(left);
      }
      if (right != 0 && right != kEmptyDirectorySentinel) {
        pending.push_back(right);
      }
    }
    return true;
  }

  std::ifstream file_;
  uint64_t partition_base_ = 0;
};

// ----------------------------------------------------------------------------
// File picker
// ----------------------------------------------------------------------------

#if defined(_WIN32)
std::filesystem::path PickIsoFile() {
  wchar_t filename[MAX_PATH] = {};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFile = filename;
  ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
  ofn.lpstrFilter = L"Xbox 360 disc image (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
  ofn.lpstrTitle = L"Select your Dante's Inferno Xbox 360 disc image";
  ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
              OFN_DONTADDTORECENT;
  if (!GetOpenFileNameW(&ofn)) {
    return {};
  }
  return filename;
}
#elif defined(__APPLE__)
std::filesystem::path PickIsoFile() {
  REXLOG_ERROR("The ISO file picker is not implemented on macOS.");
  return {};
}
#elif defined(__ANDROID__)
std::filesystem::path PickIsoFile() {
  REXLOG_INFO("Requesting ISO file via Android SAF picker");
  return dantes::android::ConsumePendingIsoPath();
}
#else
std::filesystem::path PickIsoFile() {
  if (const char* env = std::getenv("DANTES_INSTALL_ISO")) {
    if (*env != '\0') {
      return env;
    }
  }
  return {};
}
#endif

}  // namespace

bool IsGameDataInstalled(const std::filesystem::path& game_root) {
  std::error_code ec;
  return std::filesystem::is_regular_file(game_root / "default.xex", ec) && !ec;
}

bool InstallGameDataFromIso(const std::filesystem::path& iso_path,
                            const std::filesystem::path& game_root,
                            std::atomic<uint64_t>* copied_bytes,
                            std::atomic<uint64_t>* total_bytes,
                            std::string& error) {
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DantesISO",
                      "InstallGameDataFromIso: Starting extraction from '%s' to '%s'",
                      iso_path.string().c_str(), game_root.string().c_str());
#endif
  XdvdfsImageReader reader;
  if (!reader.Open(iso_path, error)) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "DantesISO",
                        "reader.Open failed for '%s': %s",
                        iso_path.string().c_str(), error.c_str());
#endif
    return false;
  }
  std::vector<DiscFileEntry> files;
  if (!reader.ListFiles(files, error)) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "DantesISO",
                        "reader.ListFiles failed: %s", error.c_str());
#endif
    return false;
  }
  const auto xex = std::find_if(files.begin(), files.end(), [](const DiscFileEntry& f) {
    return f.relative_path == "default.xex";
  });
  if (xex == files.end()) {
    error =
        "The disc image does not contain default.xex at its root; it is not a "
        "Dante's Inferno game disc.";
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "DantesISO", "%s", error.c_str());
#endif
    return false;
  }
  if (!reader.HasXex2Magic(*xex, error)) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "DantesISO",
                        "HasXex2Magic failed: %s", error.c_str());
#endif
    return false;
  }

  uint64_t total = 0;
  for (const auto& f : files) {
    total += f.size;
  }
  if (total_bytes) {
    total_bytes->store(total, std::memory_order_relaxed);
  }
  std::error_code ec;
  std::filesystem::create_directories(game_root, ec);
  const auto space = std::filesystem::space(game_root, ec);
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DantesISO",
                      "Disc files: %zu, Total size: %llu MB, Disk available: %llu MB (ec=%d)",
                      files.size(),
                      static_cast<unsigned long long>(total >> 20),
                      static_cast<unsigned long long>(space.available >> 20),
                      ec.value());
#endif
  if (!ec && space.available > 0 && space.available < total + (512ull << 20)) {
    error = "Not enough free disk space to extract the game data (need ~" +
            std::to_string((total >> 30) + 1) + " GiB free).";
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "DantesISO", "%s", error.c_str());
#endif
    return false;
  }

  // Extract in disc order for sequential reads, except default.xex, which
  // goes last: it doubles as the "install complete" marker
  // (IsGameDataInstalled), so writing it only after everything else
  // guarantees an interrupted extraction re-opens the installer on next
  // launch and resumes. Files already extracted with the right size are
  // skipped, so that resume is cheap.
  std::sort(files.begin(), files.end(), [](const DiscFileEntry& a, const DiscFileEntry& b) {
    const bool a_is_marker = a.relative_path == "default.xex";
    const bool b_is_marker = b.relative_path == "default.xex";
    if (a_is_marker != b_is_marker) {
      return b_is_marker;
    }
    return a.start_sector < b.start_sector;
  });
  size_t extracted_count = 0;
  for (const auto& f : files) {
    const auto destination = game_root / f.relative_path;
    std::error_code exists_ec;
    if (std::filesystem::is_regular_file(destination, exists_ec) &&
        std::filesystem::file_size(destination, exists_ec) == f.size && !exists_ec) {
      if (copied_bytes) {
        copied_bytes->fetch_add(f.size, std::memory_order_relaxed);
      }
      continue;
    }
#if defined(__ANDROID__)
    if (extracted_count % 20 == 0 || f.relative_path == "default.xex") {
      __android_log_print(ANDROID_LOG_INFO, "DantesISO",
                          "Extracting [%zu/%zu]: %s (%u KB)",
                          extracted_count + 1, files.size(),
                          f.relative_path.string().c_str(), f.size >> 10);
    }
#endif
    if (!reader.ExtractFile(f, destination, copied_bytes, error)) {
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_ERROR, "DantesISO",
                          "Failed to extract '%s': %s",
                          f.relative_path.string().c_str(), error.c_str());
#endif
      return false;
    }
    extracted_count++;
  }
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DantesISO",
                      "Successfully extracted %zu files (%llu MiB) into '%s'",
                      files.size(), static_cast<unsigned long long>(total >> 20),
                      game_root.string().c_str());
#endif
  REXLOG_INFO("Extracted {} files ({} MiB) from {} into {}", files.size(), total >> 20,
              iso_path.string(), game_root.string());
  return true;
}

void ShowIsoInstallWizard(rex::ui::ImGuiDrawer* drawer,
                          rex::PathConfig runtime_paths,
                          std::function<void(rex::PathConfig)> complete) {
  (void)drawer;
  std::thread([runtime_paths = std::move(runtime_paths), complete = std::move(complete)]() mutable {
    const auto game_root = runtime_paths.game_data_root;
    REXLOG_INFO("ISO Installer: Locating Dante's Inferno ISO...");

    auto iso_path = PickIsoFile();
    if (iso_path.empty()) {
      REXLOG_ERROR("ISO Installer: No ISO file selected or selection cancelled.");
      return;
    }

    std::atomic<uint64_t> copied_bytes{0};
    std::atomic<uint64_t> total_bytes{0};
    std::string error;
    REXLOG_INFO("ISO Installer: Extracting game data from {} to {}...", iso_path.string(), game_root.string());

    if (!InstallGameDataFromIso(iso_path, game_root, &copied_bytes, &total_bytes, error)) {
      REXLOG_ERROR("ISO Installer: Game data installation failed: {}", error);
      return;
    }

    if (!IsGameDataInstalled(game_root)) {
      REXLOG_ERROR("ISO Installer: Game data could not be verified after extraction.");
      return;
    }

    REXLOG_INFO("ISO Installer: Game data extracted and verified successfully!");

    // Clean up cached ISO if it was copied to cache to free storage
    std::error_code rm_ec;
    if (iso_path.string().find("/cache/") != std::string::npos) {
      std::filesystem::remove(iso_path, rm_ec);
    }

    if (complete) {
      complete(std::move(runtime_paths));
    }
  }).detach();
}

}  // namespace dantes
