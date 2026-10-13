#include "plugin/services/UpdateService.h"

#include <fstream>

#include "bakkesmod/wrappers/http/HttpWrapper.h"

namespace rlt {

namespace {
constexpr const char* kDllName = "rltrainer.dll";
constexpr uintmax_t kMinDllSize = 64 * 1024;
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
    std::string url;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (state_.phase != Phase::Available || release_.dllUrl.empty()) return;
        url = release_.dllUrl;
        state_.phase = Phase::Downloading;
        state_.progress = 0.f;
        state_.message = "Téléchargement de la version " + release_.version + "...";
    }
    const auto dir = pluginDir();
    const auto target = dir / "rltrainer.dll.download";
    CurlRequest req;
    req.url = url;
    req.verb = "GET";
    req.headers["User-Agent"] = "RLTrainer-BakkesMod-Plugin";
    auto alive = alive_;
    req.progress_function = [this, alive](double total, double done, double, double) {
        if (!*alive || total <= 0.0) return;
        std::lock_guard<std::mutex> lk(mutex_);
        state_.progress = static_cast<float>(done / total);
    };
    HttpWrapper::SendCurlRequest(req, target.wstring(), [this, alive, dir](int code, std::wstring path) {
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
                std::filesystem::remove(file, ec);
                setState(Phase::Error, "Téléchargement invalide (code " + std::to_string(code) + "). Réessaie plus tard.");
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
            setState(Phase::Error, "Erreur pendant la mise à jour.");
        }
    });
}

} // namespace rlt
