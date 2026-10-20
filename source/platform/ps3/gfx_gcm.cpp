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

namespace gfx {

// ---------------------------------------------------------------- config
static const u32 CB_SIZE = 0x100000;              // 1 MB command buffer
static const u32 HOST_SIZE = 32 * 1024 * 1024;     // host-mapped IO area
static const int COLOR_BUFFER_NUM = 2;
static const int MAX_QUEUE_FRAMES = 1;
static const int VB_COUNT = 3;
static const u32 VB_BYTES = 2 * 1024 * 1024;       // per frame: ~14.5k sprites

static const u32 LABEL_PREPARED_BUFFER_OFFSET = 0x41;
static const u32 LABEL_BUFFER_STATUS_OFFSET = 0x42;
static const u32 LABEL_VB_FENCE = 0x50;           // 0x50..0x52
static const u32 BUFFER_IDLE = 0, BUFFER_BUSY = 1;

struct Vertex {
	float x, y, u, v;
	uint8_t r, g, b, a;
	float tr, tg, tb, hue;
};
static_assert(sizeof(Vertex) == 36, "vertex layout");

// ---------------------------------------------------------------- state
static u32 display_width, display_height, color_pitch, depth_pitch;
static u32 color_offset[COLOR_BUFFER_NUM], depth_offset;
static CellGcmContextData *ctx;

static volatile u32 g_buffer_on_display, g_buffer_flipped, g_on_flip;
static u32 g_frame_index;

static CGprogram vpo, fpo;
static void *vp_ucode;
static u32 fp_offset;
static int attr_pos, attr_uv, attr_col, attr_tone;

static Vertex *vb[VB_COUNT];
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

// ---------------------------------------------------------------- RSX local memory allocator
struct Block { u32 off, size; bool free; };
static std::vector<Block> heap;
static uintptr_t heap_base;
static u32 heap_total, heap_used;

static void heap_init(uintptr_t base, u32 size) {
	heap_base = base; heap_total = size; heap_used = 0;
	heap.clear();
	heap.push_back({ 0, size, true });
}

static void *heap_alloc(u32 size, u32 align) {
	size = (size + 127) & ~127u;
	for (size_t i = 0; i < heap.size(); i++) {
		Block b = heap[i];
		if (!b.free) continue;
		u32 start = (b.off + align - 1) & ~(align - 1);
		u32 pad = start - b.off;
		if (b.size < pad + size) continue;
		std::vector<Block> repl;
		if (pad) repl.push_back({ b.off, pad, true });
		repl.push_back({ start, size, false });
		if (b.size > pad + size) repl.push_back({ start + size, b.size - pad - size, true });
		heap.erase(heap.begin() + i);
		heap.insert(heap.begin() + i, repl.begin(), repl.end());
		heap_used += size;
		return (void *)(heap_base + start);
	}
	return nullptr;
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
static void on_flip(const u32 head) {
	(void)head;
	u32 v = g_buffer_flipped;
	for (u32 i = g_buffer_on_display; i != v; i = (i + 1) % COLOR_BUFFER_NUM)
		*(volatile u32 *)cellGcmGetLabelAddress(LABEL_BUFFER_STATUS_OFFSET + i) = BUFFER_IDLE;
	g_buffer_on_display = v;
	g_on_flip = 0;
}

static void on_vblank(const u32 head) {
	(void)head;
	u32 data = *(volatile u32 *)cellGcmGetLabelAddress(LABEL_PREPARED_BUFFER_OFFSET);
	u32 buffer_to_flip = data >> 8, index_to_flip = data & 0x7;
	if (g_on_flip == 0 && buffer_to_flip != g_buffer_on_display) {
		g_on_flip = 1;
		if (cellGcmSetFlipImmediate((u8)index_to_flip) != 0) { g_on_flip = 0; return; }
		g_buffer_flipped = buffer_to_flip;
	}
}

static int ppu_too_fast() {
	u32 gpu = *(volatile u32 *)cellGcmGetLabelAddress(LABEL_PREPARED_BUFFER_OFFSET) >> 8;
	return (((g_frame_index + COLOR_BUFFER_NUM - gpu) % COLOR_BUFFER_NUM) > (u32)MAX_QUEUE_FRAMES);
}

static void set_render_target(u32 idx) {
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

static void set_draw_env() {
	cellGcmSetColorMask(ctx, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
	cellGcmSetColorMaskMrt(ctx, 0);
	float scale[4] = { display_width * 0.5f, display_height * -0.5f, 0.5f, 0.0f };
	float offset[4] = { display_width * 0.5f, display_height * 0.5f, 0.5f, 0.0f };
	cellGcmSetViewport(ctx, 0, 0, display_width, display_height, 0.0f, 1.0f, scale, offset);
	rsxSetScissor(ctx, 0, 0, display_width, display_height);
	cellGcmSetDepthTestEnable(ctx, GCM_FALSE);
	cellGcmSetCullFaceEnable(ctx, GCM_FALSE);
	blend_set = false;
	apply_blend(Blend::Normal);
	cellGcmSetVertexProgram(ctx, vpo, vp_ucode);
	cellGcmSetFragmentProgram(ctx, fpo, fp_offset);
}

static void bind_vertex_arrays(int i) {
	u32 base = vb_offset[i];
	cellGcmSetVertexDataArray(ctx, attr_pos, 0, sizeof(Vertex), 2, CELL_GCM_VERTEX_F, CELL_GCM_LOCATION_LOCAL, base + 0);
	cellGcmSetVertexDataArray(ctx, attr_uv, 0, sizeof(Vertex), 2, CELL_GCM_VERTEX_F, CELL_GCM_LOCATION_LOCAL, base + 8);
	cellGcmSetVertexDataArray(ctx, attr_col, 0, sizeof(Vertex), 4, CELL_GCM_VERTEX_UB, CELL_GCM_LOCATION_LOCAL, base + 16);
	cellGcmSetVertexDataArray(ctx, attr_tone, 0, sizeof(Vertex), 4, CELL_GCM_VERTEX_F, CELL_GCM_LOCATION_LOCAL, base + 20);
}

static void bind_texture(const Texture *t) {
	CellGcmTexture tx;
	memset(&tx, 0, sizeof(tx));
	tx.format = CELL_GCM_TEXTURE_A8R8G8B8 | CELL_GCM_TEXTURE_LN | CELL_GCM_TEXTURE_NR;
	tx.mipmap = 1;
	tx.dimension = CELL_GCM_TEXTURE_DIMENSION_2;
	tx.cubemap = CELL_GCM_FALSE;
	tx.remap = (CELL_GCM_TEXTURE_REMAP_REMAP << 8) | (CELL_GCM_TEXTURE_REMAP_REMAP << 10)
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
	cellGcmSetTextureFilter(ctx, 0, 0, CELL_GCM_TEXTURE_NEAREST, CELL_GCM_TEXTURE_NEAREST,
	                        CELL_GCM_TEXTURE_CONVOLUTION_QUINCUNX);
	cellGcmSetTextureAddress(ctx, 0, CELL_GCM_TEXTURE_CLAMP_TO_EDGE, CELL_GCM_TEXTURE_CLAMP_TO_EDGE,
	                         CELL_GCM_TEXTURE_CLAMP_TO_EDGE, CELL_GCM_TEXTURE_UNSIGNED_REMAP_NORMAL,
	                         CELL_GCM_TEXTURE_ZFUNC_LESS, 0);
}

static void flush_batch() {
	u32 n = vb_used - batch_first;
	if (n == 0 || !batch_tex) return;
	apply_blend(batch_blend);
	bind_texture(batch_tex);
	rsxDrawVertexArray(ctx, GCM_TYPE_QUADS, batch_first, n);
	cur.draw_calls++;
	batch_first = vb_used;
}

// ---------------------------------------------------------------- public API
bool init() {
	void *host_addr = memalign(1024 * 1024, HOST_SIZE);
	if (cellGcmInit(CB_SIZE, HOST_SIZE, host_addr) != 0) { sys::log("gfx: cellGcmInit failed\n"); return false; }

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
	cellGcmSetFlipMode(GCM_FLIP_VSYNC);
	ctx = CELL_GCM_CURRENT;

	CellGcmConfig cfg;
	cellGcmGetConfiguration(&cfg);
	bump = (uintptr_t)cfg.localAddress;
	uintptr_t local_end = (uintptr_t)cfg.localAddress + cfg.localSize;

	u32 color_size = color_pitch * display_height;
	void *color_base = bump_alloc(COLOR_BUFFER_NUM * color_size, 64);
	for (int i = 0; i < COLOR_BUFFER_NUM; i++) {
		cellGcmAddressToOffset((void *)((uintptr_t)color_base + (u32)i * color_size), &color_offset[i]);
		cellGcmSetDisplayBuffer((u8)i, color_offset[i], color_pitch, display_width, display_height);
	}
	cellGcmAddressToOffset(bump_alloc(depth_pitch * display_height, 64), &depth_offset);

	for (int i = 0; i < VB_COUNT; i++) {
		vb[i] = (Vertex *)bump_alloc(VB_BYTES, 128);
		cellGcmAddressToOffset(vb[i], &vb_offset[i]);
		vb_fence_value[i] = 0;
		*(volatile u32 *)cellGcmGetLabelAddress(LABEL_VB_FENCE + i) = 0;
	}

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

	cellGcmCgUploadInternalConsts(ctx, vpo);
	set_draw_env();
	set_render_target(g_frame_index);
	cellGcmSetWriteCommandLabel(ctx, LABEL_BUFFER_STATUS_OFFSET + g_frame_index, BUFFER_BUSY);
	return true;
}

void shutdown() {
	static u32 finish_ref = 0x1000;
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
	uint8_t *dst = (uint8_t *)t->mem;
	for (uint32_t y = 0; y < h; y++)               // big-endian u32 = A,R,G,B bytes
		memcpy(dst + y * t->pitch, argb + y * w, w * 4);
	cellGcmAddressToOffset(t->mem, &t->gpu_offset);
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
	if (vb_fence_value[vb_cur] > frames_completed) frames_completed = vb_fence_value[vb_cur];
	release_pending();
	vb_used = 0;
	batch_first = 0;
	batch_tex = nullptr;
	overflow_warned = false;
	bind_vertex_arrays(vb_cur);
	cellGcmSetClearColor(ctx, clear_argb);
	cellGcmSetClearSurface(ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
}

void draw(const Texture *t, float dx, float dy, float dw, float dh,
          int sx, int sy, int sw, int sh, const Shade &s, Blend blend) {
	if (!t) return;
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
	const float sxn = 2.0f / display_width, syn = 2.0f / display_height;
	float l = dx * sxn - 1.0f, r = (dx + dw) * sxn - 1.0f;
	float tp = 1.0f - dy * syn, bt = 1.0f - (dy + dh) * syn;
	float iu = 1.0f / t->w, iv = 1.0f / t->h;
	float u0 = sx * iu, u1 = (sx + sw) * iu, v0 = sy * iv, v1 = (sy + sh) * iv;
	auto c8 = [](float f) { return (uint8_t)(f <= 0 ? 0 : f >= 1 ? 255 : (int)(f * 255.0f + 0.5f)); };
	uint8_t cr = c8(s.r), cg = c8(s.g), cb = c8(s.b), ca = c8(s.a);
	float tr = s.tone_r / 255.0f, tg = s.tone_g / 255.0f, tb = s.tone_b / 255.0f;
	float hue = s.hue * (3.14159265f / 180.0f);
	Vertex *v = vb[vb_cur] + vb_used;
	v[0] = { l, tp, u0, v0, cr, cg, cb, ca, tr, tg, tb, hue };
	v[1] = { r, tp, u1, v0, cr, cg, cb, ca, tr, tg, tb, hue };
	v[2] = { r, bt, u1, v1, cr, cg, cb, ca, tr, tg, tb, hue };
	v[3] = { l, bt, u0, v1, cr, cg, cb, ca, tr, tg, tb, hue };
	vb_used += 4;
}

void end_frame() {
	flush_batch();
	// Fence: the RSX writes this once it has consumed this frame's vertices.
	frame_counter++;
	vb_fence_value[vb_cur] = frame_counter;
	cellGcmSetWriteBackEndLabel(ctx, LABEL_VB_FENCE + vb_cur, frame_counter);

	s32 qid = cellGcmSetPrepareFlip(ctx, (u8)g_frame_index);
	while (qid < 0) { usleep(1000); qid = cellGcmSetPrepareFlip(ctx, (u8)g_frame_index); }
	cellGcmSetWriteBackEndLabel(ctx, LABEL_PREPARED_BUFFER_OFFSET, (g_frame_index << 8) | (u32)qid);
	cellGcmFlush(ctx);
	while (ppu_too_fast()) usleep(1000);

	set_draw_env();
	g_frame_index = (g_frame_index + 1) % COLOR_BUFFER_NUM;
	cellGcmSetWaitLabel(ctx, LABEL_BUFFER_STATUS_OFFSET + g_frame_index, BUFFER_IDLE);
	cellGcmSetWriteCommandLabel(ctx, LABEL_BUFFER_STATUS_OFFSET + g_frame_index, BUFFER_BUSY);
	set_render_target(g_frame_index);

	st = cur;
	st.tex_bytes_used = heap_used;
	st.tex_bytes_total = heap_total;
}

void set_clip(int x, int y, int w, int h) {
	flush_batch();
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (w < 0) w = 0;
	if (h < 0) h = 0;
	rsxSetScissor(ctx, (u16)x, (u16)y, (u16)w, (u16)h);
}
void clip_off() {
	flush_batch();
	rsxSetScissor(ctx, 0, 0, display_width, display_height);
}

Stats last_stats() { return st; }
bool screenshot(const char *) { return false; }

} // namespace gfx
