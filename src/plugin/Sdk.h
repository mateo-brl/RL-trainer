#pragma once
// Single include point for the BakkesMod SDK in the plugin glue.
// <filesystem> must come first: GameWrapper::GetDataFolder() is only declared when __cpp_lib_filesystem is defined.

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/wrappers/includes.h"
#include "bakkesmod/wrappers/Engine/EngineTAWrapper.h"
#include "bakkesmod/wrappers/Engine/WorldInfoWrapper.h"
#include "bakkesmod/wrappers/GameObject/MapListWrapper.h"
#include "bakkesmod/wrappers/GameObject/MapDataWrapper.h"
#include "bakkesmod/wrappers/SettingsWrapper.h"
#include "bakkesmod/wrappers/UniqueIDWrapper.h"

#include "core/common/Math.h"

#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL)
static_assert(_ITERATOR_DEBUG_LEVEL == 0, "Build with the release CRT (/MT): pluginsdk.dll is a release build.");
#endif

namespace rlt::sdk {

inline Vec3 toVec(const Vector& v) { return {v.X, v.Y, v.Z}; }
inline Vector toVector(const Vec3& v) { return Vector(v.x, v.y, v.z); }
inline Rot toRot(const Rotator& r) { return {r.Pitch, r.Yaw, r.Roll}; }
inline Rotator toRotator(const Rot& r) { return Rotator(r.pitch, r.yaw, r.roll); }

} // namespace rlt::sdk
