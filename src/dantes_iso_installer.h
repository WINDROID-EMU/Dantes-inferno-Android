/**
 * @file        dantes_iso_installer.h
 *
 * @brief       First-run game data installer: extracts the XDVDFS (GDF) game
 *              partition of a user-supplied Dante's Inferno Xbox 360 disc
 *              image straight into game_data_root, so a fresh install is
 *              "pick your .iso, wait, play" instead of requiring a separately
 *              extracted file tree. Accepts full Redump-style images (game
 *              partition at 0xFD90000), XGD3 images (0x2080000), and bare
 *              game-partition dumps (XDVDFS at offset 0).
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

#include <rex/rex_app.h>

namespace dantes {

// True when the extracted game data tree is present in game_root (the base
// executable default.xex is the marker the rest of the runtime keys off).
bool IsGameDataInstalled(const std::filesystem::path& game_root);

// Extracts every file of the disc image's game partition into game_root.
// copied_bytes / total_bytes may be null (headless use). Files already
// present with the correct size are skipped, so an interrupted extraction
// resumes instead of starting over.
bool InstallGameDataFromIso(const std::filesystem::path& iso_path,
                            const std::filesystem::path& game_root,
                            std::atomic<uint64_t>* copied_bytes,
                            std::atomic<uint64_t>* total_bytes,
                            std::string& error);

void ShowIsoInstallWizard(rex::ui::ImGuiDrawer* drawer,
                          rex::PathConfig runtime_paths,
                          std::function<void(rex::PathConfig)> complete);

#if defined(__ANDROID__)
namespace android {
void SetPendingIsoPath(const std::string& path);
std::filesystem::path ConsumePendingIsoPath();
}  // namespace android
#endif

}  // namespace dantes
