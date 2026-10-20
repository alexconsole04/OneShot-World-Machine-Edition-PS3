/*
 * xnb.h - minimal reader for MonoGame/XNA .xnb Texture2D files.
 *
 * Supports uncompressed XNB (flags without 0x80/0x40) holding a single
 * Texture2D in SurfaceFormat.Color (RGBA8), which is what OneShot WME ships.
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
	XNB_ERR_FORMAT,      /* surface format other than Color          */
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

/* Read a whole file into a malloc'd buffer. */
int xnb_read_file(const char *path, uint8_t **data, size_t *size);

const char *xnb_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif
