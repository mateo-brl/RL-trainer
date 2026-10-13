#include "core/modules/maps/MapScan.h"

#include <algorithm>
#include <set>

#include "core/modules/maps/MapCatalog.h"

namespace rlt {

namespace fs = std::filesystem;

std::string pathToUtf8(const fs::path& p) {
    try {
        const std::u8string u = p.u8string();
        return std::string(u.begin(), u.end());
    } catch (...) {
        return {};
    }
}

fs::path pathFromUtf8(const std::string& utf8) {
    try {
        const std::u8string u(utf8.begin(), utf8.end());
        return fs::path(u);
    } catch (...) {
        return {};
    }
}

bool isTexturePackage(const std::string& fileName) {
    static const char* kTextures[] = {"editorlandscaperesources.upk", "editormaterials.upk", "editormeshes.upk",
                                      "editorresources.upk", "engine_mi_shaders.upk", "enginebuildings.upk",
                                      "enginedebugmaterials.upk", "enginematerials.upk", "engineresources.upk",
                                      "enginevolumetrics.upk", "maptemplateindex.upk", "maptemplates.upk", "mods.upk",
                                      "nodebuddies.upk"};
    const std::string lower = toLowerAscii(fileName);
    for (const char* t : kTextures)
        if (lower == t) return true;
    return false;
}

namespace {
std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::string unquote(std::string s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        s = s.substr(1, s.size() - 2);
    return s;
}

bool hasNonAscii(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0x80) != 0; });
}
} // namespace

std::vector<std::string> splitFolderList(const std::string& text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= text.size() && out.size() < 16) {
        size_t j = text.find(';', i);
        if (j == std::string::npos) j = text.size();
        const std::string item = trim(unquote(trim(text.substr(i, j - i))));
        if (!item.empty() && std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
        i = j + 1;
    }
    return out;
}

std::string parseWmlMapsFolder(const std::string& cfgText) {
    size_t i = 0;
    while (i < cfgText.size()) {
        size_t j = cfgText.find('\n', i);
        if (j == std::string::npos) j = cfgText.size();
        const std::string line = trim(cfgText.substr(i, j - i));
        i = j + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line.rfind("//", 0) == 0) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (toLowerAscii(trim(line.substr(0, eq))) != "mapsfolderpath") continue;
        return trim(unquote(trim(line.substr(eq + 1))));
    }
    return {};
}

namespace {

struct Candidate {
    fs::path file;
    bool udk = false;
    std::uintmax_t size = 0;
};

// Better map file of one folder: .udk first, then the biggest file.
bool better(const Candidate& a, const Candidate& b) {
    if (a.udk != b.udk) return a.udk;
    return a.size > b.size;
}

void addMap(const Candidate& c, const std::string& folder, const std::string& source, std::vector<ScannedMap>& out) {
    ScannedMap m;
    m.file = c.file;
    m.path = pathToUtf8(c.file);
    if (m.path.empty()) return;
    m.stem = pathToUtf8(c.file.stem());
    m.folder = folder;
    m.source = source;
    m.size = c.size;
    m.nonAscii = hasNonAscii(m.path);
    const CatalogMap* cat = nullptr;
    if (isWorkshopId(folder)) {
        m.id = folder;
        cat = MapCatalog::findById(folder);
    }
    if (!cat) cat = MapCatalog::findByStem(m.stem);
    if (!cat && !folder.empty() && !isWorkshopId(folder)) cat = MapCatalog::findByStem(folder);
    if (m.id.empty()) m.id = (cat && !cat->ids.empty()) ? cat->ids.front() : mapStatsKey("", m.stem);
    if (cat) {
        m.inCatalog = true;
        m.title = cat->title;
        m.category = cat->category;
    } else {
        m.title = (!folder.empty() && !isWorkshopId(folder)) ? folder : prettifyMapName(m.stem);
        m.category = guessMapCategory(m.title + " " + m.stem);
    }
    out.push_back(std::move(m));
}

// Map files directly inside `dir` (not recursive).
void collect(const fs::path& dir, std::vector<Candidate>& files, const std::atomic<bool>* stop) {
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    size_t seen = 0;
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec || (stop && stop->load()) || ++seen > 4000) break;
        const fs::directory_entry& e = *it;
        std::error_code ec2;
        if (!e.is_regular_file(ec2) || ec2) continue;
        const std::string ext = toLowerAscii(pathToUtf8(e.path().extension()));
        if (ext != ".udk" && ext != ".upk") continue;
        if (isTexturePackage(pathToUtf8(e.path().filename()))) continue;
        Candidate c;
        c.file = e.path();
        c.udk = ext == ".udk";
        c.size = e.file_size(ec2);
        if (ec2) c.size = 0;
        files.push_back(std::move(c));
    }
}

} // namespace

void scanMapRoot(const fs::path& root, const std::string& source, std::vector<ScannedMap>& out,
                 const std::atomic<bool>* stop, size_t maxEntries) {
    try {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec) || ec) return;
        const fs::path base = root.lexically_normal();
        const size_t limit = out.size() + maxEntries;

        // Files directly in the root (legacy "mods/<file>.upk" layout): one map per file.
        std::vector<Candidate> direct;
        collect(base, direct, stop);
        std::sort(direct.begin(), direct.end(), [](const Candidate& a, const Candidate& b) { return a.file < b.file; });
        for (const auto& c : direct) {
            if (out.size() >= limit) return;
            addMap(c, "", source, out);
        }

        // One map per sub-folder.
        fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
        if (ec) return;
        std::vector<fs::path> dirs;
        for (const fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec || (stop && stop->load()) || dirs.size() >= maxEntries) break;
            std::error_code ec2;
            if (it->is_directory(ec2) && !ec2) dirs.push_back(it->path());
        }
        std::sort(dirs.begin(), dirs.end());
        for (const auto& d : dirs) {
            if ((stop && stop->load()) || out.size() >= limit) return;
            std::vector<Candidate> files;
            collect(d, files, stop);
            if (files.empty()) continue;
            const auto best = std::min_element(files.begin(), files.end(), better);
            addMap(*best, pathToUtf8(d.filename()), source, out);
        }
    } catch (...) {
        // Filesystem errors (permissions, encoding) never escape: the scan just returns what it found.
    }
}

void finalizeScan(std::vector<ScannedMap>& maps) {
    std::vector<ScannedMap> out;
    out.reserve(maps.size());
    std::set<std::string> paths, ids;
    for (auto& m : maps) {
        const std::string p = toLowerAscii(pathToUtf8(m.file.lexically_normal()));
        if (!paths.insert(p).second) continue;
        if (isWorkshopId(m.id) && !ids.insert(m.id).second) continue;
        out.push_back(std::move(m));
    }
    std::stable_sort(out.begin(), out.end(), [](const ScannedMap& a, const ScannedMap& b) {
        const std::string ta = toLowerAscii(a.title), tb = toLowerAscii(b.title);
        if (ta != tb) return ta < tb;
        return a.path < b.path;
    });
    maps.swap(out);
}

} // namespace rlt
