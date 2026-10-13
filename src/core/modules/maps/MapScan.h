#pragma once
// Installed custom maps scan (standard library only, no SDK): <root>/<folder>/<file>.udk|.upk (and files
// directly in <root>), .udk preferred over .upk, texture packages skipped, numeric folder = workshop id.
// A folder holding several .udk files is a collection: one map per .udk, identified by its file name.
// Runs on a worker thread in the plugin (MapsService); every function here is thread-safe (no shared state).
//
// Paths: keep std::filesystem::path end to end and convert with pathToUtf8 / pathFromUtf8. Never call
// path.string() on user paths (it throws on Windows for characters outside the ANSI code page).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/common/Context.h"

namespace rlt {

struct ScannedMap {
    std::filesystem::path file;  // absolute map file
    std::string path;            // same, UTF-8
    std::string stem;            // file name without extension (UTF-8)
    std::string folder;          // parent folder name (UTF-8), empty for files directly in the root
    std::string id;              // workshop id, or mapStatsKey of the stem
    std::string title;           // catalogue title, folder name or readable stem
    MapCategory category = MapCategory::Other;
    std::string source;          // "Steam", "Mods", "Workshop Map Loader", "Dossier perso"
    bool inCatalog = false;
    bool nonAscii = false;       // path has non-ASCII characters (load_workshop may fail)
    std::uintmax_t size = 0;
};

// UTF-8 text of a path (never throws; "" on conversion failure).
std::string pathToUtf8(const std::filesystem::path& p);
// Path from UTF-8 text (never throws; empty path on failure).
std::filesystem::path pathFromUtf8(const std::string& utf8);

// Workshop Map Loader texture packages ("EditorMaterials.upk", ...), case-insensitive.
bool isTexturePackage(const std::string& fileName);
// "C:\a; D:\b ;;\"E:\c\"" -> {"C:\a", "D:\b", "E:\c"} (trimmed, quotes removed, duplicates dropped, max 16).
std::vector<std::string> splitFolderList(const std::string& text);
// Value of MapsFolderPath in workshopmaploader.cfg ("" when absent). Format: MapsFolderPath = "C:\...".
std::string parseWmlMapsFolder(const std::string& cfgText);

// Appends the maps found under `root` (depth 2). `stop` (optional) aborts early. Never throws.
void scanMapRoot(const std::filesystem::path& root, const std::string& source, std::vector<ScannedMap>& out,
                 const std::atomic<bool>* stop = nullptr, size_t maxEntries = 2000);
// Drops duplicates (same file, or same workshop id: the first source wins) and sorts by title.
void finalizeScan(std::vector<ScannedMap>& maps);

} // namespace rlt
