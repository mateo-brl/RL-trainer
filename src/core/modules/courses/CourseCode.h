#pragma once
// Shareable course code: "RLT<version>-<T><DDD>-<13 Crockford base32 chars of the 64-bit seed>-<check>",
// e.g. "RLT1-A050-000000000001A-N" (aerial, difficulty 50, seed 42). T: A aerial, G ground & walls,
// D air dribble, S slalom. The check character (weighted sum mod 31 of the base32 values) catches every single
// wrong character (except 0 <-> Z) and every swap of two different neighbouring characters.

#include <cstdint>
#include <optional>
#include <string>

#include "core/modules/courses/CourseGen.h"

namespace rlt::courses {

constexpr size_t kCodeLength = 25;

struct DecodedCode {
    uint64_t seed = 0;
    CourseType type = CourseType::Aerial;
    int difficultyPct = 0;
};

std::string encodeCourseCode(uint64_t seed, CourseType type, int difficultyPct);
// Strict decoder (exact upper-case format, valid check character). Accepts O for 0 and I/L for 1 in the seed.
std::optional<DecodedCode> decodeCourseCode(const std::string& code);
// Cleans user input before decoding: trims, removes spaces/tabs/underscores, upper-cases ASCII letters.
std::string normalizeCourseCode(const std::string& raw);
// normalizeCourseCode + decodeCourseCode.
std::optional<DecodedCode> parseCourseCode(const std::string& raw);

} // namespace rlt::courses
