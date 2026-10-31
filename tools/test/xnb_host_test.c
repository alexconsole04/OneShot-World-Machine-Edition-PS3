/* Host-side test: parse an .xnb, print info, dump a PNG-free PPM for a look. */
#include "../source/engine/xnb.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: %s file.xnb [out.pam]\n", argv[0]); return 2; }
	uint8_t *data; size_t size;
	int r = xnb_read_file(argv[1], &data, &size);
	if (r) { printf("read: %s\n", xnb_strerror(r)); return 1; }
	xnb_texture t;
	r = xnb_parse_texture(data, size, &t, NULL);
	printf("%s: %s", argv[1], xnb_strerror(r));
	if (r) { printf("\n"); return 1; }
	printf(" %ux%u, first px 0x%08x, centre px 0x%08x\n", t.width, t.height,
	       t.argb[0], t.argb[(t.height / 2) * t.width + t.width / 2]);
	if (argc > 2) {   /* PAM with alpha so it can be converted/viewed */
		FILE *f = fopen(argv[2], "wb");
		fprintf(f, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",
		        t.width, t.height);
		for (uint32_t i = 0; i < t.width * t.height; i++) {
			uint32_t p = t.argb[i];
			unsigned char px[4] = { p >> 16, p >> 8, p, p >> 24 };
			fwrite(px, 1, 4, f);
		}
		fclose(f);
	}
	return 0;
}
