// gfx_gcm.cpp - RSX sprite renderer (libgcm).
//
// Design for PS3/RPCS3 performance:
//  - Textures live in RSX local memory (256 MB VRAM), managed by a small
//    first-fit allocator; nothing is ever read back by the PPU.
//  - Sprites are emitted as QUADS into a per-frame vertex ring in local
//    memory; consecutive sprites with the same texture+blend become one
//    draw call. Tone/hue/colour are vertex attributes, so they don't split
//    batches.
//  - Three vertex buffers, each fenced with a GPU-written label, so the PPU
//    never overwrites vertices the RSX is still reading.
#include "../gfx.h"
#include "../sys.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <vector>

#include <ppu-lv2.h>
#include <ppu-types.h>
#include <sysutil/video.h>
#include <cell/gcm.h>
#include <rsx/rsx.h>

#include "sprite_vp_vpo.h"
#include "sprite_fp_fpo.h"
#include "plain_fp_fpo.h"

namespace gfx {

// ---------------------------------------------------------------- config
static const u32 CB_SIZE = 0x100000;              // 1 MB command buffer
static const u32 HOST_SIZE = 16 * 1024 * 1024;     // host-mapped IO area (from the malloc arena)
static const int COLOR_BUFFER_NUM = 2;
static const int MAX_QUEUE_FRAMES = 1;
static const int VB_COUNT = 3;
static const u32 VB_BYTES = 2 * 1024 * 1024;       // per frame: ~14.5k sprites

static const u32 LABEL_PREPARED_BUFFER_OFFSET = 0x41;
static const u32 LABEL_BUFFER_STATUS_OFFSET = 0x42;
static const u32 LABEL_VB_FENCE = 0x50;           // 0x50..0x52
static const u32 BUFFER_IDLE = 0, BUFFER_BUSY = 1;

// All-float, 16-byte aligned attributes (48-byte stride): the most
// conventional layout for the RSX vertex fetcher (and RPCS3's emulation of
// it). The earlier packed 36-byte layout with a UB4 colour produced
// corrupted sprites under RPCS3.
struct Vertex {
	float x, y, u, v;
	float r, g, b, a;
	float tr, tg, tb, hue;
};
static_assert(sizeof(Vertex) == 48, "vertex layout");

// ---------------------------------------------------------------- state
static u32 display_width, display_height, color_pitch, depth_pitch;
static u32 tgt_w, tgt_h;
static uint8_t *color_mem[COLOR_BUFFER_NUM];   // CPU view of the screen buffers (debug reads)
static Texture *last_rt;
// After the XMB closes, the PS3 sometimes stops showing anything the 3D
// pipeline draws into the screen buffers (clears still work, off-screen
// targets are fine). Copy presentation draws the frame off-screen and
// copies it in with the copy engine. Two probe pixels in the top-left
// corner show which paths reach the screen.
static bool g_present_copy;
static Texture *frame_rt, *probe_tex, *probe_white;
static const u32 PROBE_COPY_RGB = 0x0a141e;   // copied into pixel (1,0)
static int g_draw_fails, g_checks;
static bool g_draw_ok = true, g_copy_ok = true, g_checked;
static bool g_after_system_menu;   // the screen check only acts after the XMB has been open
static volatile bool g_system_drawing;   // the XMB covers the probe pixels: no checks meanwhile
static uint8_t *depth_mem;          // size of what's being drawn into (screen or render target)
static Texture *cur_rt;
static u32 color_offset[COLOR_BUFFER_NUM], depth_offset;
static CellGcmContextData *ctx;

static volatile u32 g_buffer_on_display, g_buffer_flipped, g_on_flip;
static u32 g_frame_index;

static CGprogram vpo, fpo, fpo_plain;
static u32 fp_plain_offset;
static bool g_plain_fp;
static void *vp_ucode;
static u32 fp_offset;
static int attr_pos, attr_uv, attr_col, attr_tone;

static Vertex *vb[VB_COUNT];                 // active set (points into one of the two below)
static Vertex *vb_main_mem[VB_COUNT], *vb_local_mem[VB_COUNT];
static u32 vb_main_off[VB_COUNT], vb_local_off[VB_COUNT];
// Vertices in main (IO-mapped) memory by default: the usual home for
// per-frame dynamic geometry on PS3, and RPCS3 re-reads it on every draw
// instead of trusting a cached copy of RSX local memory written by the PPU.
static bool g_vb_main = true, g_vb_main_pending = true;
// Static index buffer: quad q -> triangles (4q,4q+1,4q+2) (4q,4q+2,4q+3).
// Indexed triangle lists are the path PS3 games use most, and the one RPCS3
// handles most robustly (QUADS need on-the-fly conversion).
static const u32 MAX_QUADS = VB_BYTES / (4 * sizeof(Vertex));
static u32 ib_offset;
static bool g_full_sync = false, g_texel_coords = true;   // texel coords: NR broke NPOT textures
static int g_draw_mode = 2;   // quads, <=256 vertices per draw (best in RPCS3 testing so far)
static u32 vb_offset[VB_COUNT];
static u32 vb_fence_value[VB_COUNT];
static u32 frame_counter;
static int vb_cur;
static u32 vb_used;          // vertices written this frame
static u32 batch_first;      // first vertex of current batch
static const Texture *batch_tex;
static Blend batch_blend;
static bool blend_set;
static Blend cur_blend;

static Stats st, cur;
static bool overflow_warned;
static uint8_t *staging;          // IO-mapped main memory for texture uploads
static u32 staging_offset, staging_size;
static u32 staging_head;   // next free byte of the upload ring
static u32 finish_ref = 0x100;
static bool tex_cache_dirty = true;   // set when the PPU uploads texture data

// ---------------------------------------------------------------- RSX local memory allocator
struct Block { u32 off, size; bool free; };
static std::vector<Block> heap;
static uintptr_t heap_base;
static u32 heap_total, heap_used;
static u32 heap_cursor;

static void heap_init(uintptr_t base, u32 size) {
	heap_base = base; heap_total = size; heap_used = 0; heap_cursor = 0;
	heap.clear();
	heap.push_back({ 0, size, true });
}

// Next-fit: keep allocating forward through VRAM and only wrap around to
// recycle freed blocks once the end is reached. Freed texture memory is
// therefore not reused until ~200 MB of later uploads have gone by, and
// textures never share a 4 KB page with each other.

static void *heap_alloc_from(u32 size, u32 align, u32 min_off) {
	for (size_t i = 0; i < heap.size(); i++) {
		Block b = heap[i];
		if (!b.free || b.off + b.size <= min_off) continue;
		u32 base = b.off < min_off ? min_off : b.off;
		u32 start = (base + align - 1) & ~(align - 1);
		if (b.off + b.size < start + size) continue;
		u32 pad = start - b.off;
		std::vector<Block> repl;
		if (pad) repl.push_back({ b.off, pad, true });
		repl.push_back({ start, size, false });
		if (b.size > pad + size) repl.push_back({ start + size, b.size - pad - size, true });
		heap.erase(heap.begin() + i);
		heap.insert(heap.begin() + i, repl.begin(), repl.end());
		heap_used += size;
		heap_cursor = start + size;
		return (void *)(heap_base + start);
	}
	return nullptr;
}

static void *heap_alloc(u32 size, u32 align) {
	if (align < 4096) align = 4096;
	size = (size + 4095) & ~4095u;
	void *p = heap_alloc_from(size, align, heap_cursor);
	if (!p) p = heap_alloc_from(size, align, 0);   // wrap around
	return p;
}

static void heap_free(void *p) {
	u32 off = (u32)((uintptr_t)p - heap_base);
	for (size_t i = 0; i < heap.size(); i++) {
		if (heap[i].off != off || heap[i].free) continue;
		heap[i].free = true;
		heap_used -= heap[i].size;
		if (i + 1 < heap.size() && heap[i + 1].free) {
			heap[i].size += heap[i + 1].size;
			heap.erase(heap.begin() + i + 1);
		}
		if (i > 0 && heap[i - 1].free) {
			heap[i - 1].size += heap[i].size;
			heap.erase(heap.begin() + i);
		}
		return;
	}
	sys::log("gfx: heap_free of unknown block\n");
}

// Simple bump allocator for fixed buffers made once at init.
static uintptr_t bump;
static void *bump_alloc(u32 size, u32 align) {
	bump = (bump + align - 1) & ~(uintptr_t)(align - 1);
	void *p = (void *)bump;
	bump += (size + 1023) & ~1023u;
	return p;
}

// ---------------------------------------------------------------- flip handling
static volatile u32 g_flip_wait, g_flip_recovered, g_flip_count, g_vblank_count, g_flip_requests, g_flip_fail;

static void finish_flip() {
	u32 v = g_buffer_flipped;
	for (u32 i = g_buffer_on_display; i != v; i = (i + 1) % COLOR_BUFFER_NUM)
		*(volatile u32 *)cellGcmGetLabelAddress(LABEL_BUFFER_STATUS_OFFSET + i) = BUFFER_IDLE;
	g_buffer_on_display = v;
	g_on_flip = 0;
}

static void on_flip(const u32 head) {
	(void)head;
	g_flip_count++;
	if (g_on_flip) finish_flip();
}

static void on_vblank(const u32 head) {
	(void)head;
	g_vblank_count++;
	if (g_on_flip) {
		// While the XMB is open the flip-done handler can be skipped; waiting
		// for it would stop the game drawing for good (a black screen until
		// the XMB was opened again). Check the flip status directly instead,
		// and as a last resort give up on that flip after half a second.
		if (cellGcmGetFlipStatus() == 0 || ++g_flip_wait > 30) {
			finish_flip();
			g_flip_recovered++;
		}
		return;   // issue the next flip on the next vblank
	}
	u32 data = *(volatile u32 *)cellGcmGetLabelAddress(LABEL_PREPARED_BUFFER_OFFSET);
	u32 buffer_to_flip = data >> 8, index_to_flip = data & 0x7;
	if (buffer_to_flip != g_buffer_on_display) {
		g_on_flip = 1;
		g_flip_wait = 0;
		cellGcmResetFlipStatus();
		g_flip_requests++;
		if (cellGcmSetFlipImmediate((u8)index_to_flip) != 0) { g_on_flip = 0; g_flip_fail++; return; }
		g_buffer_flipped = buffer_to_flip;
	}
}

static int ppu_too_fast() {
	u32 gpu = *(volatile u32 *)cellGcmGetLabelAddress(LABEL_PREPARED_BUFFER_OFFSET) >> 8;
	return (((g_frame_index + COLOR_BUFFER_NUM - gpu) % COLOR_BUFFER_NUM) > (u32)MAX_QUEUE_FRAMES);
}

static void bind_color_buffer(u32 idx) {
	CellGcmSurface sf;
	memset(&sf, 0, sizeof(sf));
	sf.colorFormat = GCM_SURFACE_X8R8G8B8;
	sf.colorTarget = GCM_SURFACE_TARGET_0;
	sf.colorLocation[0] = GCM_LOCATION_RSX;
	sf.colorOffset[0] = color_offset[idx];
	sf.colorPitch[0] = color_pitch;
	for (int i = 1; i < 4; i++) { sf.colorLocation[i] = GCM_LOCATION_RSX; sf.colorPitch[i] = 64; }
	sf.depthFormat = GCM_SURFACE_ZETA_Z16;
	sf.depthLocation = GCM_LOCATION_RSX;
	sf.depthOffset = depth_offset;
	sf.depthPitch = depth_pitch;
	sf.type = GCM_SURFACE_TYPE_LINEAR;
	sf.antiAlias = GCM_SURFACE_CENTER_1;
	sf.width = display_width;
	sf.height = display_height;
	cellGcmSetSurface(ctx, &sf);
}

static void apply_blend(Blend b) {
	if (blend_set && b == cur_blend) return;
	blend_set = true;
	cur_blend = b;
	cellGcmSetBlendEnable(ctx, GCM_TRUE);
	if (b == Blend::Additive) // XNA BlendState.Additive
		cellGcmSetBlendFunc(ctx, CELL_GCM_SRC_ALPHA, CELL_GCM_ONE, CELL_GCM_SRC_ALPHA, CELL_GCM_ONE);
	else                      // XNA BlendState.NonPremultiplied
		cellGcmSetBlendFunc(ctx, CELL_GCM_SRC_ALPHA, CELL_GCM_ONE_MINUS_SRC_ALPHA,
		                         CELL_GCM_ONE, CELL_GCM_ONE_MINUS_SRC_ALPHA);
	cellGcmSetBlendEquation(ctx, CELL_GCM_FUNC_ADD, CELL_GCM_FUNC_ADD);
}

static void set_viewport(u32 w, u32 h) {
	tgt_w = w;
	tgt_h = h;
	float scale[4] = { w * 0.5f, h * -0.5f, 0.5f, 0.0f };
	float offset[4] = { w * 0.5f, h * 0.5f, 0.5f, 0.0f };
	cellGcmSetViewport(ctx, 0, 0, (u16)w, (u16)h, 0.0f, 1.0f, scale, offset);
	rsxSetScissor(ctx, 0, 0, (u16)w, (u16)h);
}

static void set_draw_env() {
	cellGcmSetColorMask(ctx, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
	cellGcmSetColorMaskMrt(ctx, 0);
	set_viewport(display_width, display_height);
	cellGcmSetDepthTestEnable(ctx, GCM_FALSE);
	cellGcmSetCullFaceEnable(ctx, GCM_FALSE);
	// Everything else a 2D sprite renderer relies on, set explicitly every
	// frame: the XMB draws into the game's screen buffers with the same
	// graphics chip and can leave its own settings behind (after it closed,
	// everything drawn to the screen came out black while the off-screen
	// game window was fine).
	cellGcmSetDepthMask(ctx, GCM_FALSE);
	cellGcmSetDepthBoundsTestEnable(ctx, GCM_FALSE);
	cellGcmSetStencilTestEnable(ctx, GCM_FALSE);
	cellGcmSetTwoSidedStencilTestEnable(ctx, GCM_FALSE);
	cellGcmSetZcullEnable(ctx, GCM_FALSE, GCM_FALSE);
	cellGcmSetAlphaTestEnable(ctx, GCM_FALSE);
	cellGcmSetLogicOpEnable(ctx, GCM_FALSE);
	cellGcmSetDitherEnable(ctx, GCM_FALSE);
	cellGcmSetPolygonOffsetFillEnable(ctx, GCM_FALSE);
	cellGcmSetLineSmoothEnable(ctx, GCM_FALSE);
	cellGcmSetPolySmoothEnable(ctx, GCM_FALSE);
	cellGcmSetFrontPolygonMode(ctx, CELL_GCM_POLYGON_MODE_FILL);
	cellGcmSetBackPolygonMode(ctx, CELL_GCM_POLYGON_MODE_FILL);
	cellGcmSetShadeMode(ctx, CELL_GCM_SMOOTH);
	cellGcmSetFrontFace(ctx, CELL_GCM_CCW);
	cellGcmSetUserClipPlaneControl(ctx, CELL_GCM_USER_CLIP_PLANE_DISABLE, CELL_GCM_USER_CLIP_PLANE_DISABLE,
	                               CELL_GCM_USER_CLIP_PLANE_DISABLE, CELL_GCM_USER_CLIP_PLANE_DISABLE,
	                               CELL_GCM_USER_CLIP_PLANE_DISABLE, CELL_GCM_USER_CLIP_PLANE_DISABLE);
	cellGcmSetAntiAliasingControl(ctx, GCM_FALSE, GCM_FALSE, GCM_FALSE, 0xffff);
	cellGcmSetFragmentProgramGammaEnable(ctx, GCM_FALSE);
	cellGcmSetFrequencyDividerOperation(ctx, 0);
	cellGcmSetVertexAttribInputMask(ctx, 0xffff);
	blend_set = false;
	apply_blend(Blend::Normal);
	cellGcmSetVertexProgram(ctx, vpo, vp_ucode);
	if (g_plain_fp) cellGcmSetFragmentProgram(ctx, fpo_plain, fp_plain_offset);
	else cellGcmSetFragmentProgram(ctx, fpo, fp_offset);
}

static void bind_vertex_arrays(int i) {
	u32 base = vb_offset[i];
	u8 loc = g_vb_main ? CELL_GCM_LOCATION_MAIN : CELL_GCM_LOCATION_LOCAL;
	cellGcmSetVertexDataArray(ctx, attr_pos, 0, sizeof(Vertex), 2, CELL_GCM_VERTEX_F, loc, base + 0);
	cellGcmSetVertexDataArray(ctx, attr_uv, 0, sizeof(Vertex), 2, CELL_GCM_VERTEX_F, loc, base + 8);
	cellGcmSetVertexDataArray(ctx, attr_col, 0, sizeof(Vertex), 4, CELL_GCM_VERTEX_F, loc, base + 16);
	cellGcmSetVertexDataArray(ctx, attr_tone, 0, sizeof(Vertex), 4, CELL_GCM_VERTEX_F, loc, base + 32);
}

static void bind_texture(const Texture *t) {
	CellGcmTexture tx;
	memset(&tx, 0, sizeof(tx));
	tx.format = CELL_GCM_TEXTURE_A8R8G8B8 | CELL_GCM_TEXTURE_LN | (g_texel_coords ? CELL_GCM_TEXTURE_UN : CELL_GCM_TEXTURE_NR);
	tx.mipmap = 1;
	tx.dimension = CELL_GCM_TEXTURE_DIMENSION_2;
	tx.cubemap = CELL_GCM_FALSE;
	// Bits 8..15 select per-channel ops (A,R,G,B); screen captures come from an
	// X8R8G8B8 surface, so their alpha is forced to one.
	tx.remap = ((t->opaque ? CELL_GCM_TEXTURE_REMAP_ONE : CELL_GCM_TEXTURE_REMAP_REMAP) << 8) | (CELL_GCM_TEXTURE_REMAP_REMAP << 10)
	         | (CELL_GCM_TEXTURE_REMAP_REMAP << 12) | (CELL_GCM_TEXTURE_REMAP_REMAP << 14)
	         | (CELL_GCM_TEXTURE_REMAP_FROM_A << 0) | (CELL_GCM_TEXTURE_REMAP_FROM_R << 2)
	         | (CELL_GCM_TEXTURE_REMAP_FROM_G << 4) | (CELL_GCM_TEXTURE_REMAP_FROM_B << 6);
	tx.width = t->w;
	tx.height = t->h;
	tx.depth = 1;
	tx.location = CELL_GCM_LOCATION_LOCAL;
	tx.pitch = t->pitch;
	tx.offset = t->gpu_offset;
	cellGcmSetTexture(ctx, 0, &tx);
	cellGcmSetTextureControl(ctx, 0, CELL_GCM_TRUE, 0, 0, CELL_GCM_TEXTURE_MAX_ANISO_1);
	u8 filt = t->linear ? CELL_GCM_TEXTURE_LINEAR : CELL_GCM_TEXTURE_NEAREST;
	cellGcmSetTextureFilter(ctx, 0, 0, filt, filt, CELL_GCM_TEXTURE_CONVOLUTION_QUINCUNX);
	cellGcmSetTextureAddress(ctx, 0, CELL_GCM_TEXTURE_CLAMP_TO_EDGE, CELL_GCM_TEXTURE_CLAMP_TO_EDGE,
	                         CELL_GCM_TEXTURE_CLAMP_TO_EDGE, CELL_GCM_TEXTURE_UNSIGNED_REMAP_NORMAL,
	                         CELL_GCM_TEXTURE_ZFUNC_LESS, 0);
}

static void flush_batch() {
	u32 n = vb_used - batch_first;
	if (n == 0 || !batch_tex) return;
	apply_blend(batch_blend);
	if (tex_cache_dirty) {
		// New texels were written to VRAM (possibly into memory a freed
		// texture used); drop any cached copies before sampling.
		cellGcmSetInvalidateTextureCache(ctx, CELL_GCM_INVALIDATE_TEXTURE);
		tex_cache_dirty = false;
	}
	bind_texture(batch_tex);
	// The vertex ring is reused every VB_COUNT frames: without this the
	// RSX's vertex cache can hand back vertices from an earlier frame,
	// which showed up as flickering black slivers next to sprites.
	cellGcmSetInvalidateVertexCache(ctx);
	if (g_draw_mode == 0) {
		rsxDrawIndexArray(ctx, GCM_TYPE_TRIANGLES, ib_offset + (batch_first / 4) * 6 * 4, (n / 4) * 6,
		                  GCM_INDEX_TYPE_32B, CELL_GCM_LOCATION_LOCAL);
	} else if (g_draw_mode == 1) {
		rsxDrawVertexArray(ctx, GCM_TYPE_QUADS, batch_first, n);
	} else {
		for (u32 f = batch_first, left = n; left; ) {
			u32 c = left > 256 ? 256 : left;
			rsxDrawVertexArray(ctx, GCM_TYPE_QUADS, f, c);
			f += c; left -= c;
		}
	}
	cur.draw_calls++;
	batch_first = vb_used;
}

// ---------------------------------------------------------------- public API
bool init() {
	void *host_addr = memalign(1024 * 1024, HOST_SIZE);
	if (cellGcmInit(CB_SIZE, HOST_SIZE, host_addr) != 0) { sys::log("gfx: cellGcmInit failed\n"); return false; }
	// The command buffer occupies the first CB_SIZE bytes of the IO area;
	// the rest is the staging buffer the RSX copies textures from.
	staging = (uint8_t *)host_addr + CB_SIZE;
	staging_size = HOST_SIZE - CB_SIZE - VB_COUNT * VB_BYTES;
	for (int i = 0; i < VB_COUNT; i++) {
		vb_main_mem[i] = (Vertex *)(staging + staging_size + (u32)i * VB_BYTES);
		cellGcmAddressToOffset(vb_main_mem[i], &vb_main_off[i]);
	}
	cellGcmAddressToOffset(staging, &staging_offset);

	videoState state;
	videoConfiguration vcfg;
	videoResolution res;
	if (videoGetState(0, 0, &state) != 0 || state.state != 0) return false;
	if (videoGetResolution(state.displayMode.resolution, &res) != 0) return false;
	display_width = res.width;
	display_height = res.height;
	color_pitch = display_width * 4;
	depth_pitch = display_width * 2;
	memset(&vcfg, 0, sizeof(vcfg));
	vcfg.resolution = state.displayMode.resolution;
	vcfg.format = VIDEO_BUFFER_FORMAT_XRGB;
	vcfg.pitch = color_pitch;
	vcfg.aspect = state.displayMode.aspect;
	if (videoConfigure(0, &vcfg, NULL, 0) != 0) return false;
	// The flip is issued from the vblank handler, i.e. already inside the
	// vertical blank. In VSYNC mode real hardware then waits for the *next*
	// vsync, so only every second refresh shows a new frame (30 fps; RPCS3
	// flips at once and hides this). HSYNC flips immediately - still within
	// the blanking period, so there is no tearing.
	cellGcmSetFlipMode(GCM_FLIP_HSYNC);
	ctx = CELL_GCM_CURRENT;

	CellGcmConfig cfg;
	cellGcmGetConfiguration(&cfg);
	bump = (uintptr_t)cfg.localAddress;
	uintptr_t local_end = (uintptr_t)cfg.localAddress + cfg.localSize;

	u32 color_size = color_pitch * display_height;
	void *color_base = bump_alloc(COLOR_BUFFER_NUM * color_size, 64);
	for (int i = 0; i < COLOR_BUFFER_NUM; i++) color_mem[i] = (uint8_t *)color_base + (u32)i * color_size;
	for (int i = 0; i < COLOR_BUFFER_NUM; i++) {
		cellGcmAddressToOffset((void *)((uintptr_t)color_base + (u32)i * color_size), &color_offset[i]);
		cellGcmSetDisplayBuffer((u8)i, color_offset[i], color_pitch, display_width, display_height);
	}
	depth_mem = (uint8_t *)bump_alloc(depth_pitch * display_height, 64);
	cellGcmAddressToOffset(depth_mem, &depth_offset);

	for (int i = 0; i < VB_COUNT; i++) {
		vb_local_mem[i] = (Vertex *)bump_alloc(VB_BYTES, 128);
		cellGcmAddressToOffset(vb_local_mem[i], &vb_local_off[i]);
		vb[i] = g_vb_main ? vb_main_mem[i] : vb_local_mem[i];
		vb_offset[i] = g_vb_main ? vb_main_off[i] : vb_local_off[i];
		vb_fence_value[i] = 0;
		*(volatile u32 *)cellGcmGetLabelAddress(LABEL_VB_FENCE + i) = 0;
	}

	u32 *ib = (u32 *)bump_alloc(MAX_QUADS * 6 * 4, 128);
	for (u32 q = 0; q < MAX_QUADS; q++) {
		u32 b = q * 4, *o = ib + q * 6;
		o[0] = b; o[1] = b + 1; o[2] = b + 2;
		o[3] = b; o[4] = b + 2; o[5] = b + 3;
	}
	cellGcmAddressToOffset(ib, &ib_offset);

	// Shaders
	u32 vpsize = 0, fpsize = 0;
	void *fp_blob;
	vpo = (CGprogram)sprite_vp_vpo;
	fpo = (CGprogram)sprite_fp_fpo;
	cellGcmCgInitProgram(vpo);
	cellGcmCgInitProgram(fpo);
	cellGcmCgGetUCode(vpo, &vp_ucode, &vpsize);
	cellGcmCgGetUCode(fpo, &fp_blob, &fpsize);
	void *fp_ucode = bump_alloc(fpsize, 64);
	memcpy(fp_ucode, fp_blob, fpsize);
	cellGcmAddressToOffset(fp_ucode, &fp_offset);
	{
		fpo_plain = (CGprogram)plain_fp_fpo;
		cellGcmCgInitProgram(fpo_plain);
		void *blob; u32 sz = 0;
		cellGcmCgGetUCode(fpo_plain, &blob, &sz);
		void *uc = bump_alloc(sz, 64);
		memcpy(uc, blob, sz);
		cellGcmAddressToOffset(uc, &fp_plain_offset);
	}
	auto attr = [](const char *name, int fallback) {
		CGparameter p = cellGcmCgGetNamedParameter(vpo, name);
		return p ? (int)cellGcmCgGetParameterResource(vpo, p) - CG_ATTR0 : fallback;
	};
	attr_pos = attr("in_position", 0);
	attr_uv = attr("in_texcoord", 8);
	attr_col = attr("in_color", 9);
	attr_tone = attr("in_tone", 10);
	sys::log("gfx: attrs pos=%d uv=%d col=%d tone=%d\n", attr_pos, attr_uv, attr_col, attr_tone);

	// Everything after the fixed buffers is texture heap.
	bump = (bump + 4095) & ~(uintptr_t)4095;
	heap_init(bump, (u32)(local_end - bump));
	sys::log("gfx: %ux%u, texture heap %u MB\n", display_width, display_height, heap_total >> 20);

	for (int i = 0; i < COLOR_BUFFER_NUM; i++)
		*(volatile u32 *)cellGcmGetLabelAddress(LABEL_BUFFER_STATUS_OFFSET + i) = BUFFER_IDLE;
	*(volatile u32 *)cellGcmGetLabelAddress(LABEL_PREPARED_BUFFER_OFFSET) = (g_buffer_on_display << 8);
	*(volatile u32 *)cellGcmGetLabelAddress(LABEL_BUFFER_STATUS_OFFSET + g_buffer_on_display) = BUFFER_BUSY;
	g_frame_index = (g_buffer_on_display + 1) % COLOR_BUFFER_NUM;
	cellGcmSetVBlankHandler(on_vblank);
	cellGcmSetFlipHandler(on_flip);

	{
		uint32_t px = 0xff000000u | PROBE_COPY_RGB;
		probe_tex = create_texture(1, 1, &px);
		uint32_t white = 0xffffffffu;
		probe_white = create_texture(1, 1, &white);
	}
	cellGcmCgUploadInternalConsts(ctx, vpo);
	set_draw_env();
	bind_color_buffer(g_frame_index);
	cellGcmSetWriteCommandLabel(ctx, LABEL_BUFFER_STATUS_OFFSET + g_frame_index, BUFFER_BUSY);
	return true;
}

void shutdown() {
	cellGcmFinish(ctx, ++finish_ref);
	u32 last = *(volatile u32 *)cellGcmGetLabelAddress(LABEL_PREPARED_BUFFER_OFFSET) >> 8;
	while (g_buffer_on_display != last) usleep(1000);
}

int screen_w() { return (int)display_width; }
int screen_h() { return (int)display_height; }

Texture *create_texture(uint32_t w, uint32_t h, const uint32_t *argb) {
	Texture *t = new Texture;
	t->w = w; t->h = h;
	t->pitch = (w * 4 + 63) & ~63u;           // RSX-friendly row pitch
	t->bytes = t->pitch * h;
	t->mem = heap_alloc(t->bytes, 128);
	if (!t->mem) {
		sys::log("gfx: OUT OF VRAM for %ux%u texture (%u/%u used)\n", w, h, heap_used, heap_total);
		delete t;
		return nullptr;
	}
	cellGcmAddressToOffset(t->mem, &t->gpu_offset);
	// Upload with the RSX's copy engine (main memory -> VRAM) rather than
	// PPU stores into VRAM: faster on hardware, and RPCS3 sees a GPU-side
	// write, so stale cached copies of recycled memory get invalidated.
	//
	// The staging area is used as a ring: each upload gets its own slice, so
	// the PPU never has to wait for a copy to finish (cellGcmFinish also
	// waits for the whole frame queued before it, a few ms each time). Only
	// when the ring wraps does it wait, once, for every earlier copy.
	const u32 row = w * 4;
	if (t->bytes <= staging_size) {
		if (staging_head + t->bytes > staging_size) {
			cellGcmFinish(ctx, ++finish_ref);
			staging_head = 0;
		}
		uint8_t *dst = staging + staging_head;
		for (u32 y = 0; y < h; y++)                  // big-endian u32 = A,R,G,B bytes
			memcpy(dst + y * t->pitch, argb + y * w, row);
		__asm__ volatile("sync" ::: "memory");
		cellGcmSetTransferData(ctx, CELL_GCM_TRANSFER_MAIN_TO_LOCAL, t->gpu_offset, t->pitch, staging_offset + staging_head,
		                       t->pitch, row, h);
		staging_head = (staging_head + t->bytes + 127) & ~127u;
	} else {
		// Larger than the whole staging area: in pieces, waiting for each.
		cellGcmFinish(ctx, ++finish_ref);
		const u32 rows_per_chunk = staging_size / t->pitch;
		for (u32 y0 = 0; y0 < h; y0 += rows_per_chunk) {
			u32 n = h - y0 < rows_per_chunk ? h - y0 : rows_per_chunk;
			for (u32 y = 0; y < n; y++)
				memcpy(staging + y * t->pitch, argb + (y0 + y) * w, row);
			__asm__ volatile("sync" ::: "memory");
			cellGcmSetTransferData(ctx, CELL_GCM_TRANSFER_MAIN_TO_LOCAL, t->gpu_offset + y0 * t->pitch, t->pitch,
			                       staging_offset, t->pitch, row, n);
			cellGcmFinish(ctx, ++finish_ref);
		}
		staging_head = 0;
	}
	tex_cache_dirty = true;
	return t;
}

// The RSX may still sample a texture from frames already queued, so frees
// are deferred until the vertex-buffer fences prove those frames finished.
struct PendingFree { void *mem; u32 after_frame; };
static std::vector<PendingFree> pending_frees;
static u32 frames_completed;

void destroy_texture(Texture *t) {
	if (!t) return;
	// Frames up to frame_counter + 1 (the one being built) may reference it.
	pending_frees.push_back({ t->mem, frame_counter + 1 });
	if (t->aux) pending_frees.push_back({ t->aux, frame_counter + 1 });
	if (t == cur_rt) set_render_target(nullptr);
	delete t;
}

static void release_pending() {
	size_t w = 0;
	for (size_t i = 0; i < pending_frees.size(); i++) {
		if (pending_frees[i].after_frame <= frames_completed) heap_free(pending_frees[i].mem);
		else pending_frees[w++] = pending_frees[i];
	}
	pending_frees.resize(w);
}

void begin_frame(uint32_t clear_argb) {
	cur = Stats{};
	// Pick the next vertex buffer and wait for the RSX to be done with it.
	vb_cur = (vb_cur + 1) % VB_COUNT;
	volatile u32 *fence = (volatile u32 *)cellGcmGetLabelAddress(LABEL_VB_FENCE + vb_cur);
	while (*fence < vb_fence_value[vb_cur]) usleep(100);
	if (g_vb_main_pending != g_vb_main) {
		// Switching memory: drain the RSX so no queued frame still reads the old set.
		cellGcmFinish(ctx, ++finish_ref);
		g_vb_main = g_vb_main_pending;
		for (int i = 0; i < VB_COUNT; i++) {
			vb[i] = g_vb_main ? vb_main_mem[i] : vb_local_mem[i];
			vb_offset[i] = g_vb_main ? vb_main_off[i] : vb_local_off[i];
		}
	}
	if (vb_fence_value[vb_cur] > frames_completed) frames_completed = vb_fence_value[vb_cur];
	release_pending();
	vb_used = 0;
	batch_first = 0;
	batch_tex = nullptr;
	overflow_warned = false;
	bind_vertex_arrays(vb_cur);
	cellGcmSetClearColor(ctx, clear_argb);
	cellGcmSetClearDepthStencil(ctx, 0xffffff00);
	cellGcmSetClearSurface(ctx, GCM_CLEAR_Z | GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
}

// Clip rectangle (screen px), applied to every quad on the CPU so drawing
// never relies on the RSX scissor alone.
static float clip_x0 = 0, clip_y0 = 0, clip_x1 = 1e9f, clip_y1 = 1e9f;

void draw(const Texture *t, float dx, float dy, float dw, float dh,
          int sx, int sy, int sw, int sh, const Shade &s, Blend blend) {
	if (!t) return;
	float u0f = (float)sx, v0f = (float)sy, u1f = (float)(sx + sw), v1f = (float)(sy + sh);
	if (dw > 0 && dh > 0) {
		float x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
		if (x1 <= clip_x0 || y1 <= clip_y0 || x0 >= clip_x1 || y0 >= clip_y1) return;
		float du = (u1f - u0f) / dw, dv = (v1f - v0f) / dh;
		if (x0 < clip_x0) { u0f += (clip_x0 - x0) * du; x0 = clip_x0; }
		if (x1 > clip_x1) { u1f -= (x1 - clip_x1) * du; x1 = clip_x1; }
		if (y0 < clip_y0) { v0f += (clip_y0 - y0) * dv; y0 = clip_y0; }
		if (y1 > clip_y1) { v1f -= (y1 - clip_y1) * dv; y1 = clip_y1; }
		dx = x0; dy = y0; dw = x1 - x0; dh = y1 - y0;
	}
	if ((vb_used + 4) * sizeof(Vertex) > VB_BYTES) {
		if (!overflow_warned) { sys::log("gfx: vertex buffer full, sprites dropped\n"); overflow_warned = true; }
		return;
	}
	if (t != batch_tex || blend != batch_blend) {
		flush_batch();
		batch_tex = t;
		batch_blend = blend;
	}
	cur.quads++;
	const float sxn = 2.0f / tgt_w, syn = 2.0f / tgt_h;
	float l = dx * sxn - 1.0f, r = (dx + dw) * sxn - 1.0f;
	float tp = 1.0f - dy * syn, bt = 1.0f - (dy + dh) * syn;
	float iu = g_texel_coords ? 1.0f : 1.0f / t->w, iv = g_texel_coords ? 1.0f : 1.0f / t->h;
	float u0 = u0f * iu, u1 = u1f * iu, v0 = v0f * iv, v1 = v1f * iv;
	auto cl = [](float f) { return f <= 0.0f ? 0.0f : (f >= 1.0f ? 1.0f : f); };
	float cr = cl(s.r), cg = cl(s.g), cb = cl(s.b), ca = cl(s.a);
	float tr = s.tone_r / 255.0f, tg = s.tone_g / 255.0f, tb = s.tone_b / 255.0f;
	float hue = s.hue * (3.14159265f / 180.0f);
	Vertex *v = vb[vb_cur] + vb_used;
	v[0] = { l, tp, u0, v0, cr, cg, cb, ca, tr, tg, tb, hue };
	v[1] = { r, tp, u1, v0, cr, cg, cb, ca, tr, tg, tb, hue };
	v[2] = { r, bt, u1, v1, cr, cg, cb, ca, tr, tg, tb, hue };
	v[3] = { l, bt, u0, v1, cr, cg, cb, ca, tr, tg, tb, hue };
	vb_used += 4;
}

// The command buffer is a 1 MB ring. When a frame's commands reach its end,
// the toolchain's wrap handler waits until the RSX has executed everything
// queued, which includes waiting for the previous frame's flip: up to two
// vblanks (~33 ms) lost every second or two. Wrapping here instead, between
// frames and well before the end, needs no waiting: the start of the ring
// was executed long ago.
static void wrap_fifo_early() {
	const uintptr_t reserve = 256 * 1024;   // far more than one frame's commands
	if ((uintptr_t)ctx->end - (uintptr_t)ctx->current > reserve) return;
	u32 begin_off;
	if (cellGcmAddressToOffset((void *)ctx->begin, &begin_off) != 0) return;
	u32 get = cellGcmGetControlRegister()->get;
	if (get >= begin_off && get < begin_off + reserve) return;   // RSX still in that area: leave it to the handler
	cellGcmSetJumpCommand(ctx, begin_off);
	ctx->current = ctx->begin;
	__asm__ volatile("sync" ::: "memory");
	cellGcmFlush(ctx);
	static bool logged;
	if (!logged) { logged = true; sys::log("gfx: command buffer wrapped between frames\n"); }
}

static Texture *make_rt(u32 w, u32 h);
static void bind_screen();

void end_frame() {
	if (cur_rt) set_render_target(nullptr);
	// Draw probe: one pixel in the top-left corner, a known colour.
	{
		Shade ps;
		ps.r = 0.2f; ps.g = 0.4f; ps.b = 0.6f;
		clip_x0 = clip_y0 = 0; clip_x1 = clip_y1 = 1e9f;
		if (probe_white) draw(probe_white, 0, 0, 1, 1, 0, 0, 1, 1, ps, Blend::Normal);
	}
	flush_batch();
	if (g_present_copy && frame_rt) {
		cellGcmSetWaitForIdle(ctx);
		cellGcmSetTransferImage(ctx, CELL_GCM_TRANSFER_LOCAL_TO_LOCAL, color_offset[g_frame_index], color_pitch, 0, 0,
		                        frame_rt->gpu_offset, frame_rt->pitch, 0, 0, display_width, display_height, 4);
	}
	// Copy probe: one pixel from a 1x1 texture into (1,0) with the copy engine.
	if (probe_tex)
		cellGcmSetTransferImage(ctx, CELL_GCM_TRANSFER_LOCAL_TO_LOCAL, color_offset[g_frame_index], color_pitch, 1, 0,
		                        probe_tex->gpu_offset, probe_tex->pitch, 0, 0, 1, 1, 4);
	// Fence: the RSX writes this once it has consumed this frame's vertices.
	frame_counter++;
	vb_fence_value[vb_cur] = frame_counter;
	cellGcmSetWriteBackEndLabel(ctx, LABEL_VB_FENCE + vb_cur, frame_counter);

	// Make every vertex/texel the PPU wrote this frame visible to the RSX
	// before the commands that read them are kicked off.
	__asm__ volatile("sync" ::: "memory");
	s32 qid = cellGcmSetPrepareFlip(ctx, (u8)g_frame_index);
	while (qid < 0) { usleep(1000); qid = cellGcmSetPrepareFlip(ctx, (u8)g_frame_index); }
	cellGcmSetWriteBackEndLabel(ctx, LABEL_PREPARED_BUFFER_OFFSET, (g_frame_index << 8) | (u32)qid);
	cellGcmFlush(ctx);
	if (g_full_sync) cellGcmFinish(ctx, ++finish_ref);   // debug: no CPU/GPU overlap at all
	while (ppu_too_fast()) usleep(1000);
	wrap_fifo_early();

	set_draw_env();
	g_frame_index = (g_frame_index + 1) % COLOR_BUFFER_NUM;
	cellGcmSetWaitLabel(ctx, LABEL_BUFFER_STATUS_OFFSET + g_frame_index, BUFFER_IDLE);
	cellGcmSetWriteCommandLabel(ctx, LABEL_BUFFER_STATUS_OFFSET + g_frame_index, BUFFER_BUSY);

	// Every half second: did the probes reach the frame now on screen?
	if (++g_checks % 30 == 0 && frame_counter > 10 && !g_system_drawing) {
		const uint8_t *m = color_mem[g_buffer_on_display % COLOR_BUFFER_NUM];
		u32 p0 = *(volatile const u32 *)m & 0xffffff, p1 = *(volatile const u32 *)(m + 4) & 0xffffff;
		auto near = [](u32 c, int r, int g, int b) {
			int dr = (int)((c >> 16) & 255) - r, dg = (int)((c >> 8) & 255) - g, db = (int)(c & 255) - b;
			return dr > -4 && dr < 4 && dg > -4 && dg < 4 && db > -4 && db < 4;
		};
		bool draw_ok = near(p0, 51, 102, 153), copy_ok = p1 == PROBE_COPY_RGB;
		if (!g_checked || draw_ok != g_draw_ok || copy_ok != g_copy_ok)
			sys::log("gfx: screen check: drawing %s, copying %s (presenting by %s; probe pixels %06x %06x)\n",
			         draw_ok ? "reaches the screen" : "DOES NOT reach the screen", copy_ok ? "works" : "FAILS",
			         g_present_copy ? "copy" : "drawing", p0, p1);
		g_checked = true;
		g_draw_ok = draw_ok;
		g_copy_ok = copy_ok;
		g_draw_fails = draw_ok ? 0 : g_draw_fails + 1;
		// Only after the XMB: emulators (RPCS3) don't write drawn pixels back to
		// memory where the PPU can read them, so the probe would misfire there.
		if (!g_present_copy && g_after_system_menu && g_draw_fails >= 2 && copy_ok) {
			if (!frame_rt) frame_rt = make_rt(display_width, display_height);
			if (frame_rt) {
				g_present_copy = true;
				sys::log("gfx: switching to copy presentation (the screen stopped accepting drawing)\n");
			}
		}
	}
	bind_screen();

	static u32 recovered_logged;
	static uint64_t recovered_log_time;
	if (g_flip_recovered != recovered_logged && sys::time_us() - recovered_log_time > 5000000) {
		recovered_logged = g_flip_recovered;
		recovered_log_time = sys::time_us();
		sys::log("gfx: a flip finished without its handler (system menu open?), %u so far\n", recovered_logged);
	}
	st = cur;
	st.tex_bytes_used = heap_used;
	st.tex_bytes_total = heap_total;
}

static Texture *make_rt(u32 w, u32 h) { return create_render_target(w, h); }

void set_present_copy(bool on) {
	if (on && !frame_rt) frame_rt = make_rt(display_width, display_height);
	g_present_copy = on && frame_rt;
	sys::log("gfx: presenting by %s\n", g_present_copy ? "copy" : "drawing");
}
bool present_copy() { return g_present_copy; }

Texture *create_render_target(uint32_t w, uint32_t h) {
	Texture *t = new Texture;
	t->w = w; t->h = h;
	t->pitch = (w * 4 + 63) & ~63u;
	t->bytes = t->pitch * h;
	t->opaque = true;
	t->mem = heap_alloc(t->bytes, 4096);
	u32 dpitch = (w * 2 + 63) & ~63u;
	t->aux = t->mem ? heap_alloc(dpitch * h, 4096) : nullptr;   // Z16, never tested against
	if (!t->mem || !t->aux) {
		sys::log("gfx: no VRAM for a %ux%u render target\n", w, h);
		if (t->mem) heap_free(t->mem);
		delete t;
		return nullptr;
	}
	cellGcmAddressToOffset(t->mem, &t->gpu_offset);
	sys::log("gfx: render target %ux%u (%u KB)\n", w, h, (t->bytes + dpitch * h) / 1024);
	return t;
}

static void bind_rt_surface(Texture *rt) {
	CellGcmSurface sf;
	memset(&sf, 0, sizeof(sf));
	sf.colorFormat = GCM_SURFACE_X8R8G8B8;
	sf.colorTarget = GCM_SURFACE_TARGET_0;
	sf.colorLocation[0] = GCM_LOCATION_RSX;
	sf.colorOffset[0] = rt->gpu_offset;
	sf.colorPitch[0] = rt->pitch;
	for (int i = 1; i < 4; i++) { sf.colorLocation[i] = GCM_LOCATION_RSX; sf.colorPitch[i] = 64; }
	u32 doff;
	cellGcmAddressToOffset(rt->aux, &doff);
	sf.depthFormat = GCM_SURFACE_ZETA_Z16;
	sf.depthLocation = GCM_LOCATION_RSX;
	sf.depthOffset = doff;
	sf.depthPitch = (rt->w * 2 + 63) & ~63u;
	sf.type = GCM_SURFACE_TYPE_LINEAR;
	sf.antiAlias = GCM_SURFACE_CENTER_1;
	sf.width = rt->w;
	sf.height = rt->h;
	cellGcmSetSurface(ctx, &sf);
	set_viewport(rt->w, rt->h);
}

// "The screen": normally the colour buffer being drawn this frame. In copy
// presentation, a full-screen render target that end_frame copies into the
// colour buffer with the copy engine instead (see present_copy below).
static void bind_screen() {
	if (g_present_copy && frame_rt) bind_rt_surface(frame_rt);
	else {
		bind_color_buffer(g_frame_index);
		set_viewport(display_width, display_height);
	}
}

void set_render_target(Texture *rt) {
	if (rt == cur_rt) return;
	flush_batch();
	// Let draws that read or write the previous target finish before the
	// RSX starts on the next one (it may sample what was just drawn).
	cellGcmSetWaitForIdle(ctx);
	cur_rt = rt;
	if (rt) last_rt = rt;
	// Programs, constants, vertex arrays and switches again with every
	// target: after the XMB has been open, drawing stopped working after the
	// first target switch of each frame.
	cellGcmCgUploadInternalConsts(ctx, vpo);
	set_draw_env();
	bind_vertex_arrays(vb_cur);
	if (rt) {
		bind_rt_surface(rt);
	} else {
		bind_screen();
	}
	clip_x0 = clip_y0 = 0; clip_x1 = clip_y1 = 1e9f;
	tex_cache_dirty = true;   // the target's new pixels must not come from a stale texture cache
}

static Texture *cap_tex;

static bool ensure_cap_tex(u32 w, u32 h) {
	if (cap_tex && cap_tex->w == w && cap_tex->h == h) return true;
	destroy_texture(cap_tex);
	cap_tex = new Texture;
	cap_tex->w = w; cap_tex->h = h;
	cap_tex->pitch = (w * 4 + 63) & ~63u;
	cap_tex->bytes = cap_tex->pitch * h;
	cap_tex->mem = heap_alloc(cap_tex->bytes, 128);
	cap_tex->opaque = true;
	if (!cap_tex->mem) { delete cap_tex; cap_tex = nullptr; return false; }
	cellGcmAddressToOffset(cap_tex->mem, &cap_tex->gpu_offset);
	return true;
}

const Texture *capture_texture(const Texture *rt) {
	if (!rt) return nullptr;
	flush_batch();
	if (!ensure_cap_tex(rt->w, rt->h)) return nullptr;
	// Queued behind the draws that filled rt, so it copies finished pixels.
	cellGcmSetTransferImage(ctx, CELL_GCM_TRANSFER_LOCAL_TO_LOCAL, cap_tex->gpu_offset, cap_tex->pitch, 0, 0,
	                        rt->gpu_offset, rt->pitch, 0, 0, rt->w, rt->h, 4);
	tex_cache_dirty = true;
	cap_tex->linear = rt->linear;
	return cap_tex;
}

const Texture *capture_screen(int x, int y, int w, int h) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > (int)display_width) w = (int)display_width - x;
	if (y + h > (int)display_height) h = (int)display_height - y;
	if (w <= 0 || h <= 0) return nullptr;
	flush_batch();
	if (!ensure_cap_tex((u32)w, (u32)h)) return nullptr;
	cap_tex->linear = false;
	// The frame presented last; the copy is queued behind its draws in the
	// command stream, so it reads finished pixels.
	u32 src = color_offset[(g_frame_index + COLOR_BUFFER_NUM - 1) % COLOR_BUFFER_NUM];
	cellGcmSetTransferImage(ctx, CELL_GCM_TRANSFER_LOCAL_TO_LOCAL, cap_tex->gpu_offset, cap_tex->pitch, 0, 0,
	                        src, color_pitch, (u32)x, (u32)y, (u32)w, (u32)h, 4);
	tex_cache_dirty = true;
	return cap_tex;
}

void set_clip(int x, int y, int w, int h) {
	flush_batch();
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (w < 0) w = 0;
	if (h < 0) h = 0;
	rsxSetScissor(ctx, (u16)x, (u16)y, (u16)w, (u16)h);
	clip_x0 = (float)x; clip_y0 = (float)y; clip_x1 = (float)(x + w); clip_y1 = (float)(y + h);
}
void clip_off() {
	flush_batch();
	rsxSetScissor(ctx, 0, 0, (u16)tgt_w, (u16)tgt_h);
	clip_x0 = clip_y0 = 0; clip_x1 = clip_y1 = 1e9f;
}

void set_draw_mode(int m) { flush_batch(); g_draw_mode = m % 3; }
int draw_mode() { return g_draw_mode; }
const char *draw_mode_name() {
	static const char *n[] = { "indexed tris", "quads", "quads <=64/draw" };
	return n[g_draw_mode];
}

void set_plain_shader(bool on) {
	flush_batch();
	g_plain_fp = on;
	if (on) cellGcmSetFragmentProgram(ctx, fpo_plain, fp_plain_offset);
	else cellGcmSetFragmentProgram(ctx, fpo, fp_offset);
}
bool plain_shader() { return g_plain_fp; }
void set_full_sync(bool on) { g_full_sync = on; }
bool full_sync() { return g_full_sync; }
void set_texel_coords(bool on) { flush_batch(); g_texel_coords = on; }
bool texel_coords() { return g_texel_coords; }

Stats last_stats() { return st; }

void debug_status(char *buf, int n) {
	snprintf(buf, n, "presenting by %s, %u frames shown, %u flip%s recovered", g_present_copy ? "copy" : "drawing",
	         g_flip_count, g_flip_recovered, g_flip_recovered == 1 ? "" : "s");
}

void set_system_drawing(bool on) { g_system_drawing = on; g_draw_fails = 0; }

void system_menu_closed() {
	g_after_system_menu = true;
	// The XMB may change the display setup while it's open: register the
	// game's display buffers and flip mode again.
	for (int i = 0; i < COLOR_BUFFER_NUM; i++)
		cellGcmSetDisplayBuffer((u8)i, color_offset[i], color_pitch, display_width, display_height);
	cellGcmSetFlipMode(GCM_FLIP_HSYNC);
	// ...and set the drawing state again from scratch.
	set_render_target(nullptr);
	cellGcmCgUploadInternalConsts(ctx, vpo);
	set_draw_env();
	bind_vertex_arrays(vb_cur);
	bind_screen();
	tex_cache_dirty = true;
}
bool screenshot(const char *) { return false; }

} // namespace gfx

namespace gfx {
void set_vertex_main_memory(bool on) { g_vb_main_pending = on; }
bool vertex_main_memory() { return g_vb_main_pending; }
}
