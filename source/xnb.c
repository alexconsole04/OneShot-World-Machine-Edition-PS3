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

int xnb_parse_texture(const uint8_t *data, size_t size, xnb_texture *out,
                      void *(*alloc)(size_t)) {
	cursor c = { data, data + size };
	uint32_t v = 0, flags = 0, n = 0;

	memset(out, 0, sizeof(*out));
	if (!alloc) alloc = malloc;

	if (size < 10 || memcmp(data, "XNB", 3) != 0) return XNB_ERR_MAGIC;
	c.p += 3;
	rd_u8(&c, &v);            /* platform: 'w','d','x','m','a','i',... */
	rd_u8(&c, &v);            /* format version, 5 for XNA 4 / MonoGame */
	rd_u8(&c, &flags);
	if (flags & 0xC0) return XNB_ERR_COMPRESSED;
	if (!rd_u32le(&c, &v)) return XNB_ERR_TRUNCATED;   /* total file size */
	if (v > size) return XNB_ERR_TRUNCATED;

	/* Type readers.  Remember whether reader #1 is a Texture2DReader. */
	if (!rd_7bit(&c, &n) || n == 0) return XNB_ERR_TRUNCATED;
	int first_is_tex = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t len;
		if (!rd_7bit(&c, &len) || !need(&c, len)) return XNB_ERR_TRUNCATED;
		if (i == 0) {
			static const char key[] = "Texture2DReader";
			size_t kl = sizeof(key) - 1;
			for (uint32_t j = 0; j + kl <= len; j++)
				if (memcmp(c.p + j, key, kl) == 0) { first_is_tex = 1; break; }
		}
		c.p += len;
		if (!rd_u32le(&c, &v)) return XNB_ERR_TRUNCATED; /* reader version */
	}
	if (!rd_7bit(&c, &v)) return XNB_ERR_TRUNCATED;     /* shared resources */
	if (!rd_7bit(&c, &v)) return XNB_ERR_TRUNCATED;     /* primary typeId  */
	if (v != 1 || !first_is_tex) return XNB_ERR_READER;

	uint32_t fmt, w, h, mips, bytes;
	if (!rd_u32le(&c, &fmt) || !rd_u32le(&c, &w) || !rd_u32le(&c, &h) ||
	    !rd_u32le(&c, &mips) || !rd_u32le(&c, &bytes))
		return XNB_ERR_TRUNCATED;
	if (fmt != 0) return XNB_ERR_FORMAT;               /* 0 = Color (RGBA8) */
	if (mips < 1 || bytes != w * h * 4u || !need(&c, bytes)) return XNB_ERR_TRUNCATED;

	uint32_t *px = (uint32_t *)alloc((size_t)w * h * 4u);
	if (!px) return XNB_ERR_NOMEM;
	const uint8_t *s = c.p;
	for (uint32_t i = 0; i < w * h; i++, s += 4)       /* RGBA bytes -> ARGB word */
		px[i] = ((uint32_t)s[3] << 24) | ((uint32_t)s[0] << 16) |
		        ((uint32_t)s[1] << 8) | (uint32_t)s[2];

	out->width = w;
	out->height = h;
	out->argb = px;
	return XNB_OK;
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
