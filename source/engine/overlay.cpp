#include "overlay.h"
#include "gm.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define STB_EASY_FONT_IMPLEMENTATION_GUARD
#include "stb_easy_font.h"

namespace overlay {

static void draw(float x, float y, float s, unsigned rgb, const char *buf, bool backing, float alpha = 1.0f) {
	static char quads[64 * 1024];   // stb_easy_font: 4 verts x 16 bytes per quad
	int nq = stb_easy_font_print(0, 0, (char *)buf, nullptr, quads, sizeof(quads));
	float w = (float)stb_easy_font_width((char *)buf), h = (float)stb_easy_font_height((char *)buf);
	const gfx::Texture *wt = gm::white();
	gfx::Shade bg;
	bg.r = bg.g = bg.b = 0.0f;
	bg.a = 0.6f;
	if (backing) gm::blit_screen(wt, x - 3 * s, y - 3 * s, (w + 5) * s, (h + 4) * s, Rect(0, 0, 1, 1), bg);
	gfx::Shade fg;
	fg.r = ((rgb >> 16) & 255) / 255.0f;
	fg.g = ((rgb >> 8) & 255) / 255.0f;
	fg.b = (rgb & 255) / 255.0f;
	fg.a = alpha;
	for (int i = 0; i < nq; i++) {
		const float *v = (const float *)(quads + i * 64);   // x,y,z,color per vertex
		float x0 = v[0], y0 = v[1], x1 = v[8], y1 = v[9];  // vertices 0 and 2
		gm::blit_screen(wt, x + x0 * s, y + y0 * s, (x1 - x0) * s, (y1 - y0) * s, Rect(0, 0, 1, 1), fg);
	}
}

void text(float x, float y, float s, unsigned rgb, const char *fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	draw(x, y, s, rgb, buf, true);
}

void text_plain(float x, float y, float s, unsigned rgb, const char *str, float alpha) { draw(x, y, s, rgb, str, false, alpha); }

int text_width(const char *str) { return stb_easy_font_width((char *)str); }

} // namespace overlay
