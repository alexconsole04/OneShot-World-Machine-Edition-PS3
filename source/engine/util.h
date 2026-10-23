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

// Frame profiler: mark() the end of each phase; the slow-frame log names the
// phase that took longest.
namespace prof {
void begin();                    // start of a frame
void mark(const char *phase);    // end of a phase (string literal)
const char *worst(float *ms);    // longest phase of the frame
void report(const char *what);   // every 10 s: fps, missed vblanks, biggest phases (log)
}
