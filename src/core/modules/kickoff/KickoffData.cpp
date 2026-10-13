#include "core/modules/kickoff/KickoffData.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace rlt {

namespace {

// Blue spawns, same table as RocketSim CAR_SPAWN_LOCATIONS_SOCCAR / RLBot wiki.
const std::array<KickoffSpawnInfo, kKoSpawnCount> kSpawns{{
    {KickoffSpawn::DiagonalLeft, KickoffSpawnKind::Diagonal, "diagonal_left", "Diagonale gauche", "diagonale gauche",
     2048.f, -2560.f, 135.f},
    {KickoffSpawn::DiagonalRight, KickoffSpawnKind::Diagonal, "diagonal_right", "Diagonale droite", "diagonale droite",
     -2048.f, -2560.f, 45.f},
    {KickoffSpawn::OffCenterLeft, KickoffSpawnKind::OffCenter, "offcenter_left", "Décalé gauche", "décalé gauche",
     256.f, -3840.f, 90.f},
    {KickoffSpawn::OffCenterRight, KickoffSpawnKind::OffCenter, "offcenter_right", "Décalé droite", "décalé droite",
     -256.f, -3840.f, 90.f},
    {KickoffSpawn::BackCenter, KickoffSpawnKind::BackCenter, "back", "Centre arrière", "centre arrière", 0.f, -4608.f,
     90.f},
}};

std::string lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

const KickoffSpawnInfo& kickoffSpawnInfo(KickoffSpawn s) {
    const int i = std::clamp(static_cast<int>(s), 0, kKoSpawnCount - 1);
    return kSpawns[static_cast<size_t>(i)];
}

KickoffSpawn kickoffSpawnFromIndex(int i) { return static_cast<KickoffSpawn>(std::clamp(i, 0, kKoSpawnCount - 1)); }

bool kickoffSpawnFromKey(const std::string& raw, KickoffSpawn& out) {
    const std::string k = lower(raw);
    struct Alias {
        const char* key;
        KickoffSpawn spawn;
    };
    static const Alias aliases[] = {
        {"diagonal_left", KickoffSpawn::DiagonalLeft},
        {"diag_left", KickoffSpawn::DiagonalLeft},
        {"diagonale_gauche", KickoffSpawn::DiagonalLeft},
        {"diagonal_right", KickoffSpawn::DiagonalRight},
        {"diag_right", KickoffSpawn::DiagonalRight},
        {"diagonale_droite", KickoffSpawn::DiagonalRight},
        {"offcenter_left", KickoffSpawn::OffCenterLeft},
        {"off_left", KickoffSpawn::OffCenterLeft},
        {"decale_gauche", KickoffSpawn::OffCenterLeft},
        {"offcenter_right", KickoffSpawn::OffCenterRight},
        {"off_right", KickoffSpawn::OffCenterRight},
        {"decale_droite", KickoffSpawn::OffCenterRight},
        {"back", KickoffSpawn::BackCenter},
        {"back_center", KickoffSpawn::BackCenter},
        {"centre", KickoffSpawn::BackCenter},
        {"center", KickoffSpawn::BackCenter},
    };
    for (const auto& a : aliases)
        if (k == a.key) {
            out = a.spawn;
            return true;
        }
    return false;
}

const char* kickoffKindSkill(KickoffSpawnKind k) {
    switch (k) {
    case KickoffSpawnKind::Diagonal: return "diagonal";
    case KickoffSpawnKind::OffCenter: return "decale";
    default: return "centre";
    }
}

const char* kickoffKindName(KickoffSpawnKind k) {
    switch (k) {
    case KickoffSpawnKind::Diagonal: return "Diagonale";
    case KickoffSpawnKind::OffCenter: return "Décalé";
    default: return "Centre";
    }
}

KickoffPose kickoffPose(KickoffSpawn s, KickoffTeam team, float z) {
    const KickoffSpawnInfo& i = kickoffSpawnInfo(s);
    KickoffPose p;
    float yawDeg = i.yawDeg;
    if (team == KickoffTeam::Blue) {
        p.pos = {i.x, i.y, z};
    } else {
        p.pos = {-i.x, -i.y, z};
        yawDeg -= 180.f;
    }
    p.yawRad = wrapPi(yawDeg * kDegToRad);
    p.rot = rotFromYawDeg(p.yawRad * kRadToDeg);
    return p;
}

bool kickoffFindSpawn(const Vec3& pos, float tolerance, KickoffSpawn& spawn, KickoffTeam& team) {
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < kKoSpawnCount; ++i) {
            const KickoffPose p = kickoffPose(static_cast<KickoffSpawn>(i), static_cast<KickoffTeam>(t));
            if (length2D(pos - p.pos) <= tolerance) {
                spawn = static_cast<KickoffSpawn>(i);
                team = static_cast<KickoffTeam>(t);
                return true;
            }
        }
    return false;
}

KickoffPath kickoffPath(KickoffSpawn s, KickoffTeam team, float contactDist) {
    const KickoffPose p = kickoffPose(s, team);
    const Vec3 toBall = flat(Vec3{0.f, 0.f, 0.f} - p.pos);
    KickoffPath k;
    k.start = flat(p.pos);
    k.dir = normalize(toBall, {0.f, 1.f, 0.f});
    k.right = {-k.dir.y, k.dir.x, 0.f};
    k.length = std::max(0.f, length(toBall) - contactDist);
    return k;
}

const std::vector<KickoffPad>& kickoffSoccarPads() {
    static const std::vector<KickoffPad> pads = [] {
        std::vector<KickoffPad> v;
        const float small[28][2] = {
            {0, -4240},    {-1792, -4184}, {1792, -4184}, {-940, -3308},  {940, -3308},  {0, -2816},    {-3584, -2484},
            {3584, -2484}, {-1788, -2300}, {1788, -2300}, {-2048, -1036}, {0, -1024},    {2048, -1036}, {-1024, 0},
            {1024, 0},     {-2048, 1036},  {0, 1024},     {2048, 1036},   {-1788, 2300}, {1788, 2300},  {-3584, 2484},
            {3584, 2484},  {0, 2816},      {-940, 3308},  {940, 3308},    {-1792, 4184}, {1792, 4184},  {0, 4240}};
        for (const auto& p : small) v.push_back({{p[0], p[1], 70.f}, false});
        const float big[6][2] = {{-3584, 0}, {3584, 0}, {-3072, 4096}, {3072, 4096}, {-3072, -4096}, {3072, -4096}};
        for (const auto& p : big) v.push_back({{p[0], p[1], 73.f}, true});
        return v;
    }();
    return pads;
}

std::vector<KickoffPadOnPath> kickoffPadsOnPath(const KickoffPath& path, float extraTolerance) {
    std::vector<KickoffPadOnPath> out;
    const auto& pads = kickoffSoccarPads();
    for (int i = 0; i < static_cast<int>(pads.size()); ++i) {
        const KickoffPad& pad = pads[static_cast<size_t>(i)];
        const float a = path.along(pad.pos);
        const float l = path.lateral(pad.pos);
        const float r = (pad.big ? kKoBigPadRadius : kKoSmallPadRadius) + extraTolerance;
        if (a >= -r && a <= path.length && std::fabs(l) < r) out.push_back({i, a, l, pad.big ? 100.f : 12.f});
    }
    std::sort(out.begin(), out.end(),
              [](const KickoffPadOnPath& a, const KickoffPadOnPath& b) { return a.along < b.along; });
    return out;
}

KickoffLevel kickoffLevelFromIndex(int i) { return static_cast<KickoffLevel>(std::clamp(i, 0, kKoLevelCount - 1)); }

const char* kickoffLevelName(KickoffLevel l) {
    switch (l) {
    case KickoffLevel::Beginner: return "Débutant";
    case KickoffLevel::Standard: return "Standard";
    case KickoffLevel::Pro: return "Pro";
    default: return "Élite";
    }
}

KickoffBenchmark kickoffBenchmark(KickoffSpawnKind k, KickoffLevel l) {
    // Rows: Débutant, Standard (double front flip, 33 boost), Pro (speedflip), Élite. See kickoff.md section 3.1.
    static constexpr KickoffBenchmark diag[] = {{2.40f, 1800.f}, {2.07f, 2100.f}, {1.95f, 2200.f}, {1.88f, 2250.f}};
    static constexpr KickoffBenchmark offc[] = {{2.65f, 1800.f}, {2.30f, 2150.f}, {2.20f, 2250.f}, {2.14f, 2280.f}};
    static constexpr KickoffBenchmark back[] = {{2.95f, 1900.f}, {2.63f, 2200.f}, {2.48f, 2280.f}, {2.44f, 2300.f}};
    const int i = std::clamp(static_cast<int>(l), 0, kKoLevelCount - 1);
    switch (k) {
    case KickoffSpawnKind::Diagonal: return diag[i];
    case KickoffSpawnKind::OffCenter: return offc[i];
    default: return back[i];
    }
}

KickoffBenchmark kickoffBenchmarkAt(KickoffSpawnKind k, float level01) {
    const float x = clamp01(level01) * static_cast<float>(kKoLevelCount - 1);
    const int lo = std::min(static_cast<int>(x), kKoLevelCount - 2);
    const float t = x - static_cast<float>(lo);
    const KickoffBenchmark a = kickoffBenchmark(k, static_cast<KickoffLevel>(lo));
    const KickoffBenchmark b = kickoffBenchmark(k, static_cast<KickoffLevel>(lo + 1));
    return {lerpf(a.arrive, b.arrive, t), lerpf(a.contactSpeed, b.contactSpeed, t)};
}

} // namespace rlt
