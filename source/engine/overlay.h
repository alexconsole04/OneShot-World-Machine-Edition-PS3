// overlay.h - debug text overlay (stb_easy_font quads through the batcher).
#pragma once

namespace overlay {
// Draw text at TV pixel (x,y), scale s, colour 0xRRGGBB, with a dark backing box.
void text(float x, float y, float s, unsigned rgb, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
} // namespace overlay
