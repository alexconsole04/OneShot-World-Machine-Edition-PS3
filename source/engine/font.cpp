#include "font.h"
#include "gm.h"
#include "overlay.h"
#include "../platform/sys.h"
#include "xnb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>

namespace font {

struct Glyph {
	Rect src;          // in the font texture
	int cropX, cropY;
	float lsb, w, rsb;
	int measure;       // ceil(MeasureString(c).X)
};

struct SpriteFont {
	explicit SpriteFont(const char *n) : name(n) {}
	std::string name;
	bool tried = false, ok = false;
	gfx::Texture *tex = nullptr;
	std::unordered_map<uint32_t, Glyph> glyphs;
	int lineSpacing = 0;
	float spacing = 0;
	uint32_t defaultChar = '?';
	bool hasDefault = false;
};

// Main fonts per type (the language's: English to start with) and the two
// backup chains.
static SpriteFont fonts[3] = { SpriteFont("volter"), SpriteFont("terminus"), SpriteFont("terminus_doc") };
static int osScale = 2;          // LanguageMetaData.font_os_scale
static float squeeze = 1.0f;     // horizontal squeeze for draw_fit
static float squeezeX0;          // window x the squeeze is anchored at
static SpriteFont backup1("noto_sans"), backup1small("noto_sans_small");
static SpriteFont backup2("noto_sans_jp"), backup2small("noto_sans_jp_small");
static uint32_t frameCounter;

static bool load(SpriteFont &f) {
	if (f.tried) return f.ok;
	f.tried = true;
	std::string rel = "content/" + f.name + ".xnb";
	uint8_t *data;
	size_t size;
	if (!sys::read_file(rel.c_str(), &data, &size)) {
		sys::log("font: %s not found\n", rel.c_str());
		return false;
	}
	xnb_spritefont sf;
	int r = xnb_parse_spritefont(data, size, &sf);
	free(data);
	if (r != XNB_OK) {
		sys::log("font: %s: %s\n", rel.c_str(), xnb_strerror(r));
		return false;
	}
	f.tex = gfx::create_texture(sf.tex.width, sf.tex.height, sf.tex.argb);
	for (uint32_t i = 0; i < sf.count; i++) {
		Glyph g;
		g.src = Rect(sf.glyphs[i].x, sf.glyphs[i].y, sf.glyphs[i].w, sf.glyphs[i].h);
		g.cropX = sf.crops[i].x;
		g.cropY = sf.crops[i].y;
		g.lsb = sf.kerning[i * 3];
		g.w = sf.kerning[i * 3 + 1];
		g.rsb = sf.kerning[i * 3 + 2];
		// SpriteFont.MeasureString for one character.
		float x = (g.lsb > 0 ? g.lsb : 0) + g.w + (g.rsb > 0 ? g.rsb : 0);
		g.measure = (int)ceilf(x);
		f.glyphs[sf.chars[i]] = g;
	}
	f.lineSpacing = sf.line_spacing;
	f.spacing = sf.spacing;
	f.hasDefault = sf.has_default != 0;
	if (f.hasDefault) f.defaultChar = sf.default_char;
	f.ok = f.tex != nullptr;
	sys::log("font: %s loaded (%u glyphs, %ux%u, line %d)\n", f.name.c_str(), sf.count, sf.tex.width,
	         sf.tex.height, sf.line_spacing);
	xnb_free_spritefont(&sf);
	return f.ok;
}

bool available(Type t) { return load(fonts[t]); }

void set_language_fonts(const char *os, const char *game, const char *small, int osFontScale) {
	const char *names[3] = { os, game, small };
	for (int i = 0; i < 3; i++) {
		if (fonts[i].name == names[i]) continue;
		if (fonts[i].tex) gfx::destroy_texture(fonts[i].tex);
		fonts[i] = SpriteFont(names[i]);   // loads on first use (GraphicsManager.ClearFontCache)
	}
	osScale = osFontScale == 1 ? 1 : 2;
}

int os_scale() { return osScale; }

void set_squeeze(float sq, float anchorX) { squeeze = sq; squeezeX0 = anchorX; }

// getFontForChar: main font, then backup 1, then backup 2. May return a
// font that lacks c (then its default character is used).
static SpriteFont *font_for(Type t, uint32_t c) {
	SpriteFont &m = fonts[t];
	if (load(m) && m.glyphs.count(c)) return &m;
	SpriteFont &b1 = t == GameSmall ? backup1small : backup1;
	if (load(b1) && b1.glyphs.count(c)) return &b1;
	SpriteFont &b2 = t == GameSmall ? backup2small : backup2;
	if (load(b2) && b2.glyphs.count(c)) return &b2;
	return m.ok ? &m : nullptr;
}

static const Glyph *glyph_of(SpriteFont *f, uint32_t c) {
	auto it = f->glyphs.find(c);
	if (it == f->glyphs.end()) it = f->glyphs.find(f->defaultChar);
	if (it == f->glyphs.end()) it = f->glyphs.find('?');
	return it == f->glyphs.end() ? nullptr : &it->second;
}

int line_spacing(Type t) {
	if (load(fonts[t])) return fonts[t].lineSpacing;
	return t == Game ? 24 : 12;
}

int utf8_len(unsigned char b) { return b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1; }

uint32_t decode_utf8(const std::string &s, size_t &i) {
	unsigned char b = (unsigned char)s[i];
	int n = utf8_len(b);
	uint32_t v = n == 1 ? b : n == 2 ? (b & 31) : n == 3 ? (b & 15) : (b & 7);
	i++;
	for (int k = 1; k < n && i < s.size(); k++, i++) v = (v << 6) | ((unsigned char)s[i] & 63);
	return v;
}

// ------------------------------------------------------------------ fallback (debug font)
// Units per debug-font pixel, so text keeps roughly the real size.
static float fallback_scale(Type t) { return t == Game ? 1.6f : 0.8f; }

static int fallback_char_width(Type t, uint32_t c) {
	char buf[2] = { (char)(c < 128 ? c : '?'), 0 };
	return (int)(overlay::text_width(buf) * fallback_scale(t) + 0.5f);
}

// ------------------------------------------------------------------ measuring
// TextSize for one character (font units), including the per-font extra.
static bool is_noto(const std::string &n) { return n == "noto_sans" || n == "noto_sans_jp"; }
static bool is_wqy(const std::string &n) { return n.compare(0, 13, "wqy-microhei_") == 0; }   // incl. the _doc ones

// TextSize(..., osFontNoResize: true) for one character: what drawing
// advances by.
static int char_width_raw(Type t, uint32_t c) {
	SpriteFont *f = font_for(t, c);
	if (!f) return fallback_char_width(t, c);
	int space = 6, extra = 1;
	const std::string &n = f->name;
	if (is_noto(n) || n == "terminus") { space = 10; extra = 2; }
	else if (n == "noto_sans_small" || n == "noto_sans_jp_small" || n == "terminus_doc") { space = 8; extra = 1; }
	else if (n == "volter") { space = 4; extra = 1; }
	else if (n == "higashi_ome") { space = 10; extra = 3; }
	else if (is_wqy(n)) { space = 4; extra = 2; }
	int w;
	if (c == ' ' || c == 0xA0) w = space + extra;
	else {
		const Glyph *g = glyph_of(f, c);
		w = (g ? g->measure : 0) + extra;
	}
	if (t == OS && osScale == 2 && is_noto(n)) w = (w + 1) / 2;
	return w;
}

// TextSize for one character: the OS font of a language whose OS font is
// drawn at half scale measures half as wide.
static int char_width(Type t, uint32_t c) {
	int w = char_width_raw(t, c);
	if (t == OS && osScale == 1) w = (w + 1) / 2;
	return w;
}

// ------------------------------------------------------------------ button glyphs
struct Token { const char *token; const char *art; const char *fallback; };
// PS3 pad: the PlayStation glyph set the PC build ships (glyph_map_playstation).
static const Token tokens[] = {
	{ "@MOVEMOUSE", "glyphs/pad_rstick", "(R-stick)" },
	{ "@MAINMENU", "glyphs/pad_ps4_options", "(START)" },
	{ "@MASHTEXT", "glyphs/pad_l2", "(L2)" },
	{ "@INVENTORY", "glyphs/pad_triangle", "(Triangle)" },
	{ "@CANCEL", "glyphs/pad_circle", "(Circle)" },
	{ "@CLICK", "glyphs/pad_r2", "(R2)" },
	{ "@MOVE", "glyphs/pad_lstick", "(D-pad)" },
	{ "@RUN", "glyphs/pad_square", "(Square)" },
	{ "@OK", "glyphs/pad_x", "(Cross)" },
};

int glyph_token_at(const std::string &s, size_t i) {
	if (i >= s.size() || s[i] != '@') return 0;
	for (const Token &t : tokens) {
		size_t n = strlen(t.token);
		if (s.compare(i, n, t.token) == 0) return (int)n;
	}
	return 0;
}

static const Token *token_info(const std::string &s, size_t i, int len) {
	for (const Token &t : tokens)
		if ((int)strlen(t.token) == len && s.compare(i, len, t.token) == 0) return &t;
	return nullptr;
}

const char *glyph_fallback(const std::string &token) {
	for (const Token &t : tokens)
		if (token == t.token) return t.fallback;
	return token.c_str();
}

static const gfx::Texture *glyph_art(const Token *t) {
	return t ? gm::texture(t->art, gm::TheWorldMachine) : nullptr;
}

void tick() { frameCounter++; }

// ------------------------------------------------------------------ width / draw
static int plain_width(Type t, const std::string &s, size_t from, size_t to) {
	int w = 0;
	size_t i = from;
	while (i < to) w += char_width(t, decode_utf8(s, i));
	return w;
}

int width(Type t, const std::string &s, bool glyphs) {
	int w = 0;
	size_t i = 0, runStart = 0;
	while (i < s.size()) {
		int tl = glyphs ? glyph_token_at(s, i) : 0;
		if (!tl) { i += utf8_len((unsigned char)s[i]); continue; }
		w += plain_width(t, s, runStart, i);
		const Token *tk = token_info(s, i, tl);
		const gfx::Texture *art = glyph_art(tk);
		if (art) w += t == Game ? (int)art->w * 2 : (int)art->w;
		else w += plain_width(t, tk->fallback, 0, strlen(tk->fallback));
		i += tl;
		runStart = i;
	}
	return w + plain_width(t, s, runStart, s.size());
}

// drawText: one character at pos (units of `scale`).
static void draw_char(Type t, Vec2 pos, uint32_t c, unsigned rgb, float alpha, int scale, gfx::Blend blend) {
	SpriteFont *f = font_for(t, c);
	float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
	if (!f) {
		if (c == ' ') return;
		char buf[2] = { (char)(c < 128 ? c : '?'), 0 };
		float S = gm::window_scale();
		Vec2 o = gm::window_origin();
		float u = fallback_scale(t) * scale;   // window px per debug-font px
		overlay::text_plain(o.x + pos.x * scale * S, o.y + (pos.y * scale + 2 * scale) * S, u * S, rgb, buf, alpha);
		return;
	}
	const std::string &n = f->name;
	if (t == OS) {
		if (is_noto(n)) {
			if (scale == 2) { pos.x *= 2; pos.y = pos.y * 2 + 7; scale = 1; }
			else pos.y += 8;
		} else if (n == "terminus" || n == "higashi_ome" || is_wqy(n)) pos.y += 8;
		else if (n != "supertext") pos.y += 5;
	} else if (t == GameSmall) {
		if (n == "noto_sans_small" || n == "noto_sans_jp_small") pos.y += 1;
		else if (n == "higashi_ome_doc") pos.y -= 2;
		else if (is_wqy(n)) pos.y -= 1;
	} else if (n == "noto_sans" || n == "noto_sans_jp") {
		pos.y -= 2;
	}
	const Glyph *gl = glyph_of(f, c);
	if (!gl || gl->src.w <= 0 || gl->src.h <= 0) return;
	// SpriteBatch.DrawString of a single character at pos*scale, scaled.
	float x0 = gl->lsb > 0 ? gl->lsb : 0;
	int wx = pos.x * scale + (int)((x0 + gl->cropX) * scale);
	int wy = pos.y * scale + gl->cropY * scale;
	float xs = (float)scale;
	if (squeeze != 1.0f) {
		wx = (int)(squeezeX0 + (wx - squeezeX0) * squeeze);
		xs *= squeeze;
	}
	gm::blit_scaled(f->tex, Vec2(wx, wy), gl->src, xs, (float)scale, alpha, 0, blend, GameTone(), r, g, b);
}

static int draw_plain(Type t, Vec2 pos, const std::string &s, size_t from, size_t to, unsigned rgb, float alpha,
                      int scale, gfx::Blend blend, bool visible) {
	int adv = 0;
	size_t i = from;
	while (i < to) {
		uint32_t c = decode_utf8(s, i);
		if (visible) draw_char(t, Vec2(pos.x + adv, pos.y), c, rgb, alpha, scale, blend);
		adv += char_width_raw(t, c);
	}
	return adv;
}

int draw(Type t, Vec2 pos, const std::string &s, unsigned rgb, float alpha, int scale, Mode mode, gfx::Blend blend) {
	if (t == OS && osScale == 1 && scale >= 2) {
		scale /= 2;
		pos.x *= 2;
		pos.y *= 2;
	}
	int x = 0;
	size_t i = 0, runStart = 0;
	bool textVisible = mode != Mode::OnlyGlyphs;
	while (i < s.size()) {
		int tl = glyph_token_at(s, i);
		if (!tl) { i += utf8_len((unsigned char)s[i]); continue; }
		x += draw_plain(t, Vec2(pos.x + x, pos.y), s, runStart, i, rgb, alpha, scale, blend, textVisible);
		const Token *tk = token_info(s, i, tl);
		const gfx::Texture *art = glyph_art(tk);
		if (!art) {
			x += draw_plain(t, Vec2(pos.x + x, pos.y), tk->fallback, 0, strlen(tk->fallback), rgb, alpha, scale, blend,
			                mode != Mode::OnlyText);
		} else {
			int w = (int)art->w;
			int frames = (int)art->h / (w > 0 ? w : 1);
			if (frames < 1) frames = 1;
			int frame = (int)(frameCounter / 20 % (uint32_t)frames);
			Vec2 p(pos.x + x, pos.y);
			p.y -= t == Game ? 24 : t == GameSmall ? 8 : osScale == 2 ? 6 : 12;
			if (scale != 2) { p.x /= 2; p.y /= 2; }
			float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
			if (mode != Mode::OnlyText)
				gm::blit(art, p, Rect(0, w * frame, w, w), alpha, 0, blend, 2, GameTone(), r, g, b);
			x += w * (2 / scale);
		}
		i += tl;
		runStart = i;
	}
	x += draw_plain(t, Vec2(pos.x + x, pos.y), s, runStart, s.size(), rgb, alpha, scale, blend, textVisible);
	return x;
}

int draw_centered(Type t, Vec2 pos, const std::string &s, unsigned rgb, float alpha, int scale, Mode mode) {
	pos.x -= width(t, s, true) / 2;
	return draw(t, pos, s, rgb, alpha, scale, mode);
}

int draw_fit(Type t, Vec2 windowPos, const std::string &s, unsigned rgb, float alpha, int maxWidth, bool centered) {
	// TempTextureManager.GetSingleLineTexture + MainBlit(xCentered): the
	// text at window scale, squeezed (smoothly, on PC) to maxWidth.
	int w = width(t, s, true);
	if (w % 2 == 1) w--;
	int full = w + 4;
	float sq = maxWidth > 0 && full > maxWidth ? (float)maxWidth / full : 1.0f;
	int shown = (int)(full * sq);
	int x0 = centered ? windowPos.x - shown / 2 : windowPos.x;
	squeeze = sq;
	squeezeX0 = (float)x0;
	draw(t, Vec2(x0 + 2, windowPos.y), s, rgb, alpha, 1, Mode::OnlyText);
	squeeze = 1.0f;
	return shown;
}

// MathHelper.WordWrap.
std::vector<std::string> word_wrap(Type t, const std::string &input, int maxWidth) {
	std::vector<std::string> list;
	std::string line, word;
	int num = 0;
	size_t i = 0;
	while (i < input.size()) {
		char c = input[i];
		if (c == '@') {
			int tl = glyph_token_at(input, i);
			size_t sep = input.find_first_of(" \n", i);
			if (sep == std::string::npos) sep = input.size() - 1;
			const Token *tk = tl ? token_info(input, i, tl) : nullptr;
			const gfx::Texture *art = glyph_art(tk);
			if (!tk) {
				word += c;
				num += char_width(t, '@');
				i++;
			} else {
				// The PC build takes the whole word up to the separator.
				std::string w = input.substr(i, sep - i);
				num += art ? (int)art->w : width(t, tk->fallback);
				word += w;
				i = sep;
			}
		} else if (c == '\n') {
			line += word;
			word.clear();
			list.push_back(line);
			line.clear();
			num = 0;
			i++;
		} else if (c == ' ') {
			line += word + c;
			word.clear();
			num += char_width(t, ' ');
			i++;
		} else {
			size_t st = i;
			uint32_t cp = decode_utf8(input, i);
			word += input.substr(st, i - st);
			num += char_width(t, cp);
		}
		if (num <= maxWidth) continue;
		if (!line.empty()) {
			if (line.back() == ' ') line.pop_back();
			list.push_back(line);
			line.clear();
			num = width(t, word);
		} else if (!word.empty()) {
			// Break inside an over-long word: move its last character down.
			size_t k = word.size() - 1;
			while (k > 0 && ((unsigned char)word[k] & 0xC0) == 0x80) k--;
			std::string last = word.substr(k);
			list.push_back(word.substr(0, k));
			word = last;
			num = width(t, word);
		}
	}
	if (!line.empty() || !word.empty()) list.push_back(line + word);
	return list;
}

} // namespace font
