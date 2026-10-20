// util.h - MathHelper bits shared by the game code.
#pragma once
#include <stdint.h>
#include <string>

namespace util {
// MathHelper.Random(min, max): inclusive on both ends.
int random(int min, int max);
void seed(uint32_t s);
float lerp(float a, float b, float t);
std::string lower(std::string s);
bool starts_with(const std::string &s, const char *prefix);
} // namespace util
