// font.h - the game's SpriteFonts (GraphicsManager.TextBlit / TextSize /
// MathHelper.WordWrap).
//
// Fonts load from the user's content folder: the language's fonts (for
// English terminus (Game), terminus_doc (GameSmall) and volter (OS)), with
// noto_sans / noto_sans_small and then noto_sans_jp(_small) as the backup
// fonts for characters the main font lacks, exactly as the PC build does. Text is drawn one character at a time with the
// PC build's per-font spacing rules, so line widths and word wrapping
// match. Button tokens (@OK, @CANCEL, @MOVE, ...) draw as the controller
// glyph art from content/glyphs.
//
// If a font file is missing the debug font stands in, scaled to a similar
// size, so the game stays playable.
//
// Positions follow the C#: `pos` is in units of `scale` window pixels
// (scale 2 = game pixels, scale 1 = 640x480 window pixels). Widths are
// returned in the font's own units, like TextSize.
#pragma once
#include "types.h"
#include "../platform/gfx.h"
#include <string>
#include <vector>

namespace font {

enum Type { OS = 0, Game = 1, GameSmall = 2 };
enum class Mode { Normal, OnlyText, OnlyGlyphs };

bool available(Type t);                 // the real font file loaded
int line_spacing(Type t);

// TextSize(...).X. With glyphs=true, button tokens count as their art.
int width(Type t, const std::string &s, bool glyphs = false);
// TextBlit. Returns the advance in font units.
int draw(Type t, Vec2 pos, const std::string &s, unsigned rgb, float alpha = 1.0f, int scale = 2,
         Mode mode = Mode::Normal, gfx::Blend blend = gfx::Blend::Normal);
// TextBlitCentered (centred on pos.x, measured with glyphs).
int draw_centered(Type t, Vec2 pos, const std::string &s, unsigned rgb, float alpha = 1.0f, int scale = 2,
                  Mode mode = Mode::Normal);

// MathHelper.WordWrap: breaks on spaces and '\n', width in font units.
std::vector<std::string> word_wrap(Type t, const std::string &s, int maxWidth);

// GetSingleLineTexture(t, s, maxWidth) drawn at a window-pixel position:
// squeezed horizontally if wider than maxWidth. Returns the drawn width.
int draw_fit(Type t, Vec2 windowPos, const std::string &s, unsigned rgb, float alpha, int maxWidth, bool centered);

// The language's fonts (LanguageMetaData): font names for OS, Game and
// GameSmall, and the OS font scale (2 = pixel font drawn double, 1 = full
// resolution font drawn as is).
void set_language_fonts(const char *os, const char *game, const char *small, int osFontScale);
int os_scale();

// GraphicsManager.GlobalFrameCounter, for animated glyphs.
void tick();

// Button token at s[i] ("@OK", ...): its length, or 0.
int glyph_token_at(const std::string &s, size_t i);
// Plain-text name for a token, used when its art is missing ("(Cross)").
const char *glyph_fallback(const std::string &token);

// UTF-8 helpers.
uint32_t decode_utf8(const std::string &s, size_t &i);   // advances i
int utf8_len(unsigned char lead);

} // namespace font
