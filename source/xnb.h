/*
 * xnb.h - minimal reader for MonoGame/XNA .xnb Texture2D and SpriteFont files.
 *
 * Supports uncompressed XNB (flags without 0x80/0x40) holding a single
 * Texture2D (SurfaceFormat Color or DXT1/3/5) or a SpriteFont.
 * Portable C: no PS3 dependencies, so it can be unit-tested on the host.
 */
#ifndef OSPS3_XNB_H
#define OSPS3_XNB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum xnb_result {
	XNB_OK = 0,
	XNB_ERR_IO,          /* could not open/read the file            */
	XNB_ERR_MAGIC,       /* not an XNB file                          */
	XNB_ERR_COMPRESSED,  /* LZX/LZ4 compressed - not supported yet   */
	XNB_ERR_READER,      /* first type reader is not Texture2DReader */
	XNB_ERR_FORMAT,      /* surface format not Color/DXT1/DXT3/DXT5   */
	XNB_ERR_TRUNCATED,   /* file shorter than its headers claim      */
	XNB_ERR_NOMEM
};

typedef struct {
	uint32_t width;
	uint32_t height;
	/* Pixels as 0xAARRGGBB, one uint32 per pixel, row-major, top-down.
	 * Stored in host byte order, so on the big-endian PS3 the bytes in
	 * memory are A,R,G,B: exactly RSX's A8R8G8B8 layout. */
	uint32_t *argb;
} xnb_texture;

/* Parse an in-memory XNB.  On success fills *out and allocates out->argb
 * with alloc(size) (pass NULL to use malloc). */
int xnb_parse_texture(const uint8_t *data, size_t size, xnb_texture *out,
                      void *(*alloc)(size_t));

typedef struct { int32_t x, y, w, h; } xnb_rect;

/* A SpriteFont (XNA 4 layout). Glyph i draws glyphs[i] from tex at
 * crops[i].xy, advancing by kerning[3i] (left bearing), kerning[3i+1]
 * (width) and kerning[3i+2] (right bearing). */
typedef struct {
	xnb_texture tex;
	uint32_t count;
	xnb_rect *glyphs;
	xnb_rect *crops;
	uint32_t *chars;     /* Unicode code points */
	float *kerning;      /* 3 floats per glyph */
	int32_t line_spacing;
	float spacing;
	int has_default;
	uint32_t default_char;
} xnb_spritefont;

/* Parse an in-memory SpriteFont XNB (texture in Color or DXT1/3/5).
 * Everything is malloc'd; free with xnb_free_spritefont. */
int xnb_parse_spritefont(const uint8_t *data, size_t size, xnb_spritefont *out);
void xnb_free_spritefont(xnb_spritefont *f);

/* Read a whole file into a malloc'd buffer. */
int xnb_read_file(const char *path, uint8_t **data, size_t *size);

const char *xnb_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif
