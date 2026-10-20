// overlay.h - debug text overlay (stb_easy_font quads through the batcher).
#pragma once

namespace overlay {
// Draw text at TV pixel (x,y), scale s, colour 0xRRGGBB, with a dark backing box.
void text(float x, float y, float s, unsigned rgb, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
// No backing box, no formatting; text may contain '\n'.
void text_plain(float x, float y, float s, unsigned rgb, const char *str, float alpha = 1.0f);
int text_width(const char *str);   // unscaled pixels
} // namespace overlay
