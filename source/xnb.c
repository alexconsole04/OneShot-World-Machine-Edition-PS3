/*
 * xnb.c - see xnb.h.
 *
 * XNB layout (all integers little-endian on disk):
 *   "XNB" platform(u8) version(u8) flags(u8) fileSize(u32)
 *   typeReaderCount(7-bit int)
 *     { readerName(7-bit-length string) readerVersion(i32) } * count
 *   sharedResourceCount(7-bit int)
 *   primary object: typeId(7-bit int, 1-based reader index, 0 = null)
 *   Texture2D payload:
 *     surfaceFormat(i32) width(u32) height(u32) mipCount(u32)
 *     { dataSize(u32) data[dataSize] } * mipCount
 */
#include "xnb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const uint8_t *p, *end;
} cursor;

static int need(cursor *c, size_t n) { return (size_t)(c->end - c->p) >= n; }

static int rd_u8(cursor *c, uint32_t *v) {
	if (!need(c, 1)) return 0;
	*v = *c->p++;
	return 1;
}

static int rd_u32le(cursor *c, uint32_t *v) {
	if (!need(c, 4)) return 0;
	*v = (uint32_t)c->p[0] | ((uint32_t)c->p[1] << 8) |
	     ((uint32_t)c->p[2] << 16) | ((uint32_t)c->p[3] << 24);
	c->p += 4;
	return 1;
}

static int rd_7bit(cursor *c, uint32_t *v) {
	uint32_t result = 0, b;
	for (int shift = 0; shift < 35; shift += 7) {
		if (!rd_u8(c, &b)) return 0;
		result |= (b & 0x7f) << shift;
		if (!(b & 0x80)) { *v = result; return 1; }
	}
	return 0;
}

/* ---- DXT block decoding (SurfaceFormat Dxt1/Dxt3/Dxt5) ---- */
static uint32_t rgb565(uint32_t c) {
	uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
	r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
	return (r << 16) | (g << 8) | b;
}

static uint32_t mix3(uint32_t a, uint32_t b, int wa, int wb, int d) {
	uint32_t r = ((((a >> 16) & 255) * wa + ((b >> 16) & 255) * wb) / d);
	uint32_t g = ((((a >> 8) & 255) * wa + ((b >> 8) & 255) * wb) / d);
	uint32_t bl = (((a & 255) * wa + (b & 255) * wb) / d);
	return (r << 16) | (g << 8) | bl;
}

/* Decode one 4x4 colour block; alpha[16] (0..255) or NULL for DXT1 rules. */
static void dxt_color_block(const uint8_t *b, const uint8_t *alpha, uint32_t *px, uint32_t stride,
                            uint32_t bw, uint32_t bh) {
	uint32_t c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
	uint32_t pal[4];
	uint32_t a3 = 255;
	pal[0] = rgb565(c0);
	pal[1] = rgb565(c1);
	if (c0 > c1 || alpha) {
		pal[2] = mix3(pal[0], pal[1], 2, 1, 3);
		pal[3] = mix3(pal[0], pal[1], 1, 2, 3);
	} else {
		pal[2] = mix3(pal[0], pal[1], 1, 1, 2);
		pal[3] = 0;
		a3 = 0;
	}
	uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
	for (uint32_t y = 0; y < 4; y++)
		for (uint32_t x = 0; x < 4; x++) {
			uint32_t i = (bits >> (2 * (y * 4 + x))) & 3;
			uint32_t a = alpha ? alpha[y * 4 + x] : (i == 3 ? a3 : 255);
			if (x < bw && y < bh) px[y * stride + x] = (a << 24) | pal[i];
		}
}

static int decode_dxt(const uint8_t *s, uint32_t fmt, uint32_t w, uint32_t h, uint32_t *px) {
	uint32_t bx = (w + 3) / 4, by = (h + 3) / 4;
	uint8_t alpha[16];
	for (uint32_t j = 0; j < by; j++)
		for (uint32_t i = 0; i < bx; i++) {
			uint32_t *dst = px + (j * 4) * w + i * 4;
			uint32_t bw = w - i * 4 < 4 ? w - i * 4 : 4, bh = h - j * 4 < 4 ? h - j * 4 : 4;
			if (fmt == 4) {
				dxt_color_block(s, NULL, dst, w, bw, bh);
				s += 8;
			} else if (fmt == 5) {
				for (int k = 0; k < 16; k++) {
					uint32_t n = (s[k / 2] >> ((k & 1) * 4)) & 15;
					alpha[k] = (uint8_t)(n * 17);
				}
				dxt_color_block(s + 8, alpha, dst, w, bw, bh);
				s += 16;
			} else {
				uint32_t a0 = s[0], a1 = s[1], pal[8];
				pal[0] = a0; pal[1] = a1;
				if (a0 > a1) for (int k = 1; k < 7; k++) pal[k + 1] = ((7 - k) * a0 + k * a1) / 7;
				else {
					for (int k = 1; k < 5; k++) pal[k + 1] = ((5 - k) * a0 + k * a1) / 5;
					pal[6] = 0; pal[7] = 255;
				}
				uint64_t bits = 0;
				for (int k = 0; k < 6; k++) bits |= (uint64_t)s[2 + k] << (8 * k);
				for (int k = 0; k < 16; k++) alpha[k] = (uint8_t)pal[(bits >> (3 * k)) & 7];
				dxt_color_block(s + 8, alpha, dst, w, bw, bh);
				s += 16;
			}
		}
	return 1;
}

static int parse_header(cursor *c, const uint8_t *data, size_t size, const char *first_reader) {
	uint32_t v = 0, flags = 0, n = 0;
	if (size < 10 || memcmp(data, "XNB", 3) != 0) return XNB_ERR_MAGIC;
	c->p += 3;
	rd_u8(c, &v);            /* platform: 'w','d','x','m','a','i',... */
	rd_u8(c, &v);            /* format version, 5 for XNA 4 / MonoGame */
	rd_u8(c, &flags);
	if (flags & 0xC0) return XNB_ERR_COMPRESSED;
	if (!rd_u32le(c, &v)) return XNB_ERR_TRUNCATED;   /* total file size */
	if (v > size) return XNB_ERR_TRUNCATED;

	/* Type readers.  Check that reader #1 is the one we expect. */
	if (!rd_7bit(c, &n) || n == 0) return XNB_ERR_TRUNCATED;
	int first_ok = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t len;
		if (!rd_7bit(c, &len) || !need(c, len)) return XNB_ERR_TRUNCATED;
		if (i == 0) {
			size_t kl = strlen(first_reader);
			for (uint32_t j = 0; j + kl <= len; j++)
				if (memcmp(c->p + j, first_reader, kl) == 0) { first_ok = 1; break; }
		}
		c->p += len;
		if (!rd_u32le(c, &v)) return XNB_ERR_TRUNCATED; /* reader version */
	}
	if (!rd_7bit(c, &v)) return XNB_ERR_TRUNCATED;     /* shared resources */
	if (!rd_7bit(c, &v)) return XNB_ERR_TRUNCATED;     /* primary typeId  */
	if (v != 1 || !first_ok) return XNB_ERR_READER;
	return XNB_OK;
}

/* Texture2D payload: surfaceFormat width height mipCount {size data}... */
static int parse_texture_payload(cursor *c, xnb_texture *out, void *(*alloc)(size_t)) {
	uint32_t fmt, w, h, mips, bytes;
	if (!rd_u32le(c, &fmt) || !rd_u32le(c, &w) || !rd_u32le(c, &h) ||
	    !rd_u32le(c, &mips) || !rd_u32le(c, &bytes))
		return XNB_ERR_TRUNCATED;
	uint32_t expect;
	if (fmt == 0) expect = w * h * 4u;
	else if (fmt == 4) expect = ((w + 3) / 4) * ((h + 3) / 4) * 8u;
	else if (fmt == 5 || fmt == 6) expect = ((w + 3) / 4) * ((h + 3) / 4) * 16u;
	else return XNB_ERR_FORMAT;
	if (mips < 1 || bytes != expect || !need(c, bytes)) return XNB_ERR_TRUNCATED;

	uint32_t *px = (uint32_t *)alloc((size_t)w * h * 4u);
	if (!px) return XNB_ERR_NOMEM;
	const uint8_t *s = c->p;
	if (fmt == 0) {
		for (uint32_t i = 0; i < w * h; i++, s += 4)       /* RGBA bytes -> ARGB word */
			px[i] = ((uint32_t)s[3] << 24) | ((uint32_t)s[0] << 16) |
			        ((uint32_t)s[1] << 8) | (uint32_t)s[2];
	} else {
		decode_dxt(s, fmt, w, h, px);
	}
	c->p += bytes;
	/* Skip the remaining mip levels. */
	for (uint32_t m = 1; m < mips; m++) {
		uint32_t b;
		if (!rd_u32le(c, &b) || !need(c, b)) break;
		c->p += b;
	}
	out->width = w;
	out->height = h;
	out->argb = px;
	return XNB_OK;
}

int xnb_parse_texture(const uint8_t *data, size_t size, xnb_texture *out,
                      void *(*alloc)(size_t)) {
	cursor c = { data, data + size };
	memset(out, 0, sizeof(*out));
	if (!alloc) alloc = malloc;
	int r = parse_header(&c, data, size, "Texture2DReader");
	if (r != XNB_OK) return r;
	return parse_texture_payload(&c, out, alloc);
}

/* BinaryReader.ReadChar: one UTF-8 encoded character. */
static int rd_char(cursor *c, uint32_t *cp) {
	uint32_t b0;
	if (!rd_u8(c, &b0)) return 0;
	int extra = 0;
	uint32_t v = b0;
	if (b0 >= 0xF0) { extra = 3; v = b0 & 7; }
	else if (b0 >= 0xE0) { extra = 2; v = b0 & 15; }
	else if (b0 >= 0xC0) { extra = 1; v = b0 & 31; }
	for (int i = 0; i < extra; i++) {
		uint32_t b;
		if (!rd_u8(c, &b)) return 0;
		v = (v << 6) | (b & 63);
	}
	*cp = v;
	return 1;
}

static int rd_rect_list(cursor *c, uint32_t *count, xnb_rect **out) {
	uint32_t id, n;
	if (!rd_7bit(c, &id) || id == 0 || !rd_u32le(c, &n)) return 0;
	if (!need(c, (size_t)n * 16)) return 0;
	xnb_rect *r = (xnb_rect *)malloc(sizeof(xnb_rect) * (n ? n : 1));
	if (!r) return 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t a, b, w, h;
		rd_u32le(c, &a); rd_u32le(c, &b); rd_u32le(c, &w); rd_u32le(c, &h);
		r[i].x = (int32_t)a; r[i].y = (int32_t)b; r[i].w = (int32_t)w; r[i].h = (int32_t)h;
	}
	*count = n;
	*out = r;
	return 1;
}

static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

void xnb_free_spritefont(xnb_spritefont *f) {
	free(f->tex.argb);
	free(f->glyphs);
	free(f->crops);
	free(f->chars);
	free(f->kerning);
	memset(f, 0, sizeof(*f));
}

int xnb_parse_spritefont(const uint8_t *data, size_t size, xnb_spritefont *out) {
	cursor c = { data, data + size };
	memset(out, 0, sizeof(*out));
	int r = parse_header(&c, data, size, "SpriteFontReader");
	if (r != XNB_OK) return r;
	uint32_t id, n, n2, v;
	if (!rd_7bit(&c, &id) || id == 0) return XNB_ERR_TRUNCATED;      /* Texture2D object */
	r = parse_texture_payload(&c, &out->tex, malloc);
	if (r != XNB_OK) return r;
	r = XNB_ERR_TRUNCATED;
	if (!rd_rect_list(&c, &n, &out->glyphs)) goto fail;
	if (!rd_rect_list(&c, &n2, &out->crops) || n2 != n) goto fail;
	if (!rd_7bit(&c, &id) || id == 0 || !rd_u32le(&c, &n2) || n2 != n) goto fail;
	out->chars = (uint32_t *)malloc(sizeof(uint32_t) * (n ? n : 1));
	if (!out->chars) { r = XNB_ERR_NOMEM; goto fail; }
	for (uint32_t i = 0; i < n; i++)
		if (!rd_char(&c, &out->chars[i])) goto fail;
	if (!rd_u32le(&c, &v)) goto fail;
	out->line_spacing = (int32_t)v;
	if (!rd_u32le(&c, &v)) goto fail;
	out->spacing = u2f(v);
	if (!rd_7bit(&c, &id) || id == 0 || !rd_u32le(&c, &n2) || n2 != n || !need(&c, (size_t)n * 12)) goto fail;
	out->kerning = (float *)malloc(sizeof(float) * 3 * (n ? n : 1));
	if (!out->kerning) { r = XNB_ERR_NOMEM; goto fail; }
	for (uint32_t i = 0; i < n * 3; i++) { rd_u32le(&c, &v); out->kerning[i] = u2f(v); }
	if (rd_u8(&c, &v) && v) {
		if (rd_char(&c, &out->default_char)) out->has_default = 1;
	}
	out->count = n;
	return XNB_OK;
fail:
	xnb_free_spritefont(out);
	return r;
}

int xnb_read_file(const char *path, uint8_t **data, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (!f) return XNB_ERR_IO;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0) { fclose(f); return XNB_ERR_IO; }
	uint8_t *buf = (uint8_t *)malloc((size_t)len);
	if (!buf) { fclose(f); return XNB_ERR_NOMEM; }
	size_t got = fread(buf, 1, (size_t)len, f);
	fclose(f);
	if (got != (size_t)len) { free(buf); return XNB_ERR_IO; }
	*data = buf;
	*size = (size_t)len;
	return XNB_OK;
}

const char *xnb_strerror(int err) {
	switch (err) {
	case XNB_OK:             return "ok";
	case XNB_ERR_IO:         return "file not found or unreadable";
	case XNB_ERR_MAGIC:      return "not an XNB file";
	case XNB_ERR_COMPRESSED: return "compressed XNB not supported";
	case XNB_ERR_READER:     return "not a Texture2D XNB";
	case XNB_ERR_FORMAT:     return "unsupported surface format";
	case XNB_ERR_TRUNCATED:  return "truncated XNB";
	case XNB_ERR_NOMEM:      return "out of memory";
	}
	return "unknown error";
}
