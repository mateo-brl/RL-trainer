// Installed maps scan on a synthetic folder tree (std::filesystem, temp directory).
#include <atomic>
#include <filesystem>
#include <fstream>

#include "tests/TestFramework.h"

#include "core/modules/maps/MapScan.h"

using namespace rlt;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path root;
    explicit TempDir(const std::string& name) {
        root = fs::temp_directory_path() / ("rlt_maps_test_" + name);
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void file(const fs::path& rel, size_t bytes = 16) const {
        std::error_code ec;
        fs::create_directories((root / rel).parent_path(), ec);
        std::ofstream out(root / rel, std::ios::binary);
        out << std::string(bytes, 'x');
    }
};

const ScannedMap* byId(const std::vector<ScannedMap>& v, const std::string& id) {
    for (const auto& m : v)
        if (m.id == id) return &m;
    return nullptr;
}

} // namespace

TEST(maps_scan_steam_layout_prefers_udk_and_skips_textures) {
    TempDir t("steam");
    t.file("2025564083/LethamyrGiantRings.udk", 100);
    t.file("2025564083/LethamyrGiantRings.upk", 5000);  // bigger, but .udk wins
    t.file("2025564083/preview.jpg");
    t.file("964271505/DribbleChallenge2.udk");
    t.file("12345678/SomeUnknownRings_P.udk");
    t.file("12345678/EditorMaterials.upk");              // texture package
    t.file("emptyfolder/readme.txt");
    std::vector<ScannedMap> out;
    scanMapRoot(t.root, "Steam", out);
    CHECK(out.size() == 3);
    const ScannedMap* giant = byId(out, "2025564083");
    CHECK(giant != nullptr);
    if (giant) {
        CHECK(giant->title == "Lethamyr's Giant Rings");
        CHECK(giant->inCatalog);
        CHECK(giant->category == MapCategory::Rings);
        CHECK(giant->stem == "LethamyrGiantRings");
        CHECK(giant->path.size() > 4 && giant->path.substr(giant->path.size() - 4) == ".udk");
        CHECK(giant->source == "Steam");
        CHECK(giant->folder == "2025564083");
        CHECK(!giant->nonAscii);
    }
    const ScannedMap* unknown = byId(out, "12345678");
    CHECK(unknown != nullptr);
    if (unknown) {
        CHECK(!unknown->inCatalog);
        CHECK(unknown->title == "Some Unknown Rings");
        CHECK(unknown->category == MapCategory::Rings);
    }
    CHECK(byId(out, "964271505") != nullptr && byId(out, "964271505")->category == MapCategory::Dribble);
}

TEST(maps_scan_mods_layout_titles_and_direct_files) {
    TempDir t("mods");
    t.file("Air Dribble Hoops/airdribblehoops.upk");      // catalogue by stem: gets the Steam id
    t.file("My Cool Map/coolmap_antifreeze.upk");         // folder name as title
    t.file("Labs_Underpass_P.upk");                       // legacy swap file directly in the root
    t.file("mods.upk");                                   // texture package at the root
    std::vector<ScannedMap> out;
    scanMapRoot(t.root, "Mods", out);
    CHECK(out.size() == 3);
    const ScannedMap* hoops = byId(out, "2100268170");
    CHECK(hoops != nullptr && hoops->title == "Air Dribble Hoops" && hoops->category == MapCategory::AirDribble);
    bool cool = false, labs = false;
    for (const auto& m : out) {
        if (m.title == "My Cool Map") {
            cool = true;
            CHECK(m.id == "coolmap");
            CHECK(m.folder == "My Cool Map");
        }
        if (m.stem == "Labs_Underpass_P") {
            labs = true;
            CHECK(m.folder.empty());
            CHECK(m.title == "Labs Underpass");
        }
    }
    CHECK(cool && labs);
}

TEST(maps_scan_dedup_sort_and_bad_roots) {
    TempDir a("dedup_a");
    TempDir b("dedup_b");
    a.file("2025564083/GiantRings.udk");
    b.file("Giant Rings/GiantRings.upk");  // same map from another source (catalogue id)
    b.file("Zeta/zeta.udk");
    b.file("Alpha/alpha.udk");
    std::vector<ScannedMap> out;
    scanMapRoot(a.root, "Steam", out);
    scanMapRoot(b.root, "Mods", out);
    scanMapRoot(a.root, "Steam", out);  // same root twice
    scanMapRoot(fs::path(), "x", out);
    scanMapRoot(a.root / "does_not_exist", "x", out);
    finalizeScan(out);
    CHECK(out.size() == 3);
    CHECK(out[0].title == "alpha" || out[0].title == "Alpha");
    CHECK(out.back().title == "Zeta");
    const ScannedMap* giant = byId(out, "2025564083");
    CHECK(giant != nullptr && giant->source == "Steam");  // the first source wins
}

TEST(maps_scan_stop_flag_and_limits) {
    TempDir t("stop");
    for (int i = 0; i < 5; ++i) t.file("m" + std::to_string(i) + "/map" + std::to_string(i) + ".udk");
    std::atomic<bool> stop{true};
    std::vector<ScannedMap> out;
    scanMapRoot(t.root, "x", out, &stop);
    CHECK(out.empty());
    stop = false;
    scanMapRoot(t.root, "x", out, &stop, 2);
    CHECK(out.size() == 2);
}

TEST(maps_scan_non_ascii_paths) {
    TempDir t("utf8");
    const std::string folder = "Cartes \xC3\xA9t\xC3\xA9";  // "Cartes été"
    const fs::path dir = t.root / pathFromUtf8(folder);
    std::error_code ec;
    fs::create_directories(dir, ec);
    { std::ofstream(dir / "ring_course.udk") << "x"; }
    std::vector<ScannedMap> out;
    scanMapRoot(t.root, "Dossier perso", out);
    CHECK(out.size() == 1);
    if (!out.empty()) {
        CHECK(out[0].nonAscii);
        CHECK(out[0].folder == folder);
        CHECK(out[0].title == folder);
        CHECK(pathFromUtf8(out[0].path) == out[0].file);
    }
    CHECK(pathToUtf8(pathFromUtf8("a/\xC3\xA9")) == "a/\xC3\xA9");
}

TEST(maps_scan_texture_list_and_folder_parsing) {
    CHECK(isTexturePackage("EditorLandscapeResources.upk"));
    CHECK(isTexturePackage("NODEBUDDIES.UPK"));
    CHECK(isTexturePackage("mods.upk"));
    CHECK(!isTexturePackage("GiantRings.upk"));
    const auto f = splitFolderList(" C:\\Maps ; \"D:\\My Maps\";;C:\\Maps; ");
    CHECK(f.size() == 2);
    CHECK(f[0] == "C:\\Maps" && f[1] == "D:\\My Maps");
    CHECK(splitFolderList("").empty());
    std::string many;
    for (int i = 0; i < 40; ++i) many += "f" + std::to_string(i) + ";";
    CHECK(splitFolderList(many).size() == 16);
}

TEST(maps_scan_wml_config_parsing) {
    const std::string cfg =
        "# comment\nLanguage = \"0\"\r\nMapsFolderPath = \"D:\\RL\\maps\"\r\nPluginVersion = \"1.16\"\n";
    CHECK(parseWmlMapsFolder(cfg) == "D:\\RL\\maps");
    CHECK(parseWmlMapsFolder("mapsfolderpath='E:\\x'") == "E:\\x");
    CHECK(parseWmlMapsFolder("Language = \"0\"\n") == "");
    CHECK(parseWmlMapsFolder("") == "");
    CHECK(parseWmlMapsFolder("; MapsFolderPath = \"C:\\nope\"\n") == "");
}
