#include "plugin/services/UpdateService.h"

#include <algorithm>
#include <fstream>
#include <utility>

#include "bakkesmod/wrappers/http/HttpWrapper.h"

namespace rlt {

namespace {
constexpr const char* kDllName = "rltrainer.dll";
constexpr uintmax_t kMinDllSize = 64 * 1024;

// A short, printable hint from a failed download body (GitHub storage answers XML with a <Code> element).
std::string errorDetail(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string body(512, '\0');
    in.read(&body[0], static_cast<std::streamsize>(body.size()));
    body.resize(static_cast<size_t>(std::max<std::streamsize>(0, in.gcount())));
    std::string text;
    const size_t open = body.find("<Code>");
    const size_t close = body.find("</Code>");
    if (open != std::string::npos && close != std::string::npos && close > open) text = body.substr(open + 6, close - open - 6);
    else text = body;
    std::string out;
    for (char c : text) {
        if (out.size() >= 60) break;
        if (c >= 32 && c < 127) out += c;
        else if (!out.empty() && out.back() != ' ') out += ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? std::string() : " : " + out;
}
} // namespace

UpdateService::UpdateService(std::shared_ptr<GameWrapper> gw, std::string repoSlug, std::string currentVersion)
    : gw_(std::move(gw)), repo_(std::move(repoSlug)) {
    state_.current = std::move(currentVersion);
}

std::filesystem::path UpdateService::pluginDir() const { return gw_->GetBakkesModPath() / "plugins"; }

void UpdateService::setState(Phase p, const std::string& msg) {
    std::lock_guard<std::mutex> lk(mutex_);
    state_.phase = p;
    state_.message = msg;
}

UpdateService::State UpdateService::state() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return state_;
}

void UpdateService::cleanup() {
    std::error_code ec;
    const auto dir = pluginDir();
    std::filesystem::remove(dir / "rltrainer.dll.old", ec);
    std::filesystem::remove(dir / "rltrainer.dll.download", ec);
}

void UpdateService::checkAsync() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (state_.phase == Phase::Checking || state_.phase == Phase::Downloading) return;
        state_.phase = Phase::Checking;
        state_.message = "Recherche de mise à jour...";
    }
    CurlRequest req;
    req.url = "https://api.github.com/repos/" + repo_ + "/releases/latest";
    req.verb = "GET";
    req.headers["User-Agent"] = "RLTrainer-BakkesMod-Plugin";
    req.headers["Accept"] = "application/vnd.github+json";
    auto alive = alive_;
    HttpWrapper::SendCurlRequest(req, [this, alive](int code, std::string body) {
        if (!*alive) return;
        try {
            ReleaseInfo info;
            if (code != 200 || !parseLatestRelease(body, info)) {
                setState(Phase::Error, code == 404 ? "Aucune version publiée pour l'instant."
                                                   : "Impossible de joindre GitHub (code " + std::to_string(code) + ").");
                return;
            }
            std::lock_guard<std::mutex> lk(mutex_);
            release_ = info;
            state_.latest = info.version;
            state_.pageUrl = info.pageUrl;
            if (compareVersions(info.version, state_.current) > 0 && !info.dllUrl.empty()) {
                state_.phase = Phase::Available;
                state_.message = "Nouvelle version " + info.version + " disponible.";
            } else {
                state_.phase = Phase::UpToDate;
                state_.message = "Tu as la dernière version (" + state_.current + ").";
            }
        } catch (...) {
            setState(Phase::Error, "Réponse de GitHub illisible.");
        }
    });
}

void UpdateService::downloadAsync() {
    std::vector<std::string> urls;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (state_.phase != Phase::Available || release_.dllUrl.empty()) return;
        urls.push_back("https://raw.githubusercontent.com/" + repo_ + "/update/v" + release_.version + "/" + kDllName);
        urls.push_back(release_.dllUrl);
        state_.phase = Phase::Downloading;
        state_.progress = 0.f;
        state_.message = "Téléchargement de la version " + release_.version + "...";
    }
    tryDownload(std::move(urls), 0, std::string());
}

void UpdateService::tryDownload(std::vector<std::string> urls, size_t index, std::string failures) {
    const auto dir = pluginDir();
    const auto target = dir / "rltrainer.dll.download";
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_.progress = 0.f;
    }
    CurlRequest req;
    req.url = urls[index];
    req.verb = "GET";
    req.headers["User-Agent"] = "RLTrainer-BakkesMod-Plugin";
    auto alive = alive_;
    req.progress_function = [this, alive](double total, double done, double, double) {
        if (!*alive || total <= 0.0) return;
        std::lock_guard<std::mutex> lk(mutex_);
        state_.progress = static_cast<float>(done / total);
    };
    HttpWrapper::SendCurlRequest(req, target.wstring(), [this, alive, dir, urls, index, failures](int code, std::wstring path) {
        if (!*alive) return;
        try {
            std::error_code ec;
            const std::filesystem::path file(path);
            const uintmax_t size = std::filesystem::file_size(file, ec);
            bool looksLikeDll = false;
            {
                std::ifstream in(file, std::ios::binary);
                char mz[2] = {0, 0};
                if (in.read(mz, 2)) looksLikeDll = mz[0] == 'M' && mz[1] == 'Z';
            }
            if (code != 200 || ec || size < kMinDllSize || !looksLikeDll) {
                std::string tried = failures.empty() ? std::string() : failures + ", ";
                tried += std::string(index == 0 ? "miroir " : "GitHub ") + std::to_string(code) + errorDetail(file);
                std::filesystem::remove(file, ec);
                if (index + 1 < urls.size()) {
                    // Next source, sent from the game thread like the first request.
                    gw_->Execute([this, alive, urls, index, tried](GameWrapper*) {
                        if (*alive) tryDownload(urls, index + 1, tried);
                    });
                    return;
                }
                setState(Phase::Error, "Téléchargement impossible (" + tried + "). Utilise l'installeur RLTrainer-Setup.exe.");
                return;
            }
            const auto current = dir / kDllName;
            const auto old = dir / "rltrainer.dll.old";
            std::filesystem::remove(old, ec);
            if (std::filesystem::exists(current)) {
                std::filesystem::rename(current, old, ec);  // allowed on a loaded DLL
                if (ec) {
                    setState(Phase::Error, "Impossible de remplacer le plugin. Utilise l'installeur RLTrainer-Setup.exe.");
                    return;
                }
            }
            std::filesystem::rename(file, current, ec);
            if (ec) {
                std::error_code ec2;
                std::filesystem::rename(old, current, ec2);  // roll back
                setState(Phase::Error, "Impossible d'installer la mise à jour. Utilise l'installeur RLTrainer-Setup.exe.");
                return;
            }
            std::lock_guard<std::mutex> lk(mutex_);
            state_.phase = Phase::Ready;
            state_.progress = 1.f;
            state_.message = "Mise à jour installée. Relance Rocket League pour l'activer.";
        } catch (...) {
            setState(Phase::Error, "Erreur pendant la mise à jour. Réessaie ou utilise l'installeur RLTrainer-Setup.exe.");
        }
    });
}

} // namespace rlt
