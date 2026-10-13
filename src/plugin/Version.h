#pragma once

#ifndef RLT_VERSION_MAJOR
#define RLT_VERSION_MAJOR 1
#endif
#ifndef RLT_VERSION_MINOR
#define RLT_VERSION_MINOR 0
#endif
#ifndef RLT_VERSION_PATCH
#define RLT_VERSION_PATCH 0
#endif

#ifndef RLT_REPO_SLUG
#define RLT_REPO_SLUG "mateo-brl/RL-trainer"
#endif

#define RLT_STRINGIFY_(a) #a
#define RLT_STRINGIFY(a) RLT_STRINGIFY_(a)

constexpr const char* kPluginVersionString =
    RLT_STRINGIFY(RLT_VERSION_MAJOR) "." RLT_STRINGIFY(RLT_VERSION_MINOR) "." RLT_STRINGIFY(RLT_VERSION_PATCH);
