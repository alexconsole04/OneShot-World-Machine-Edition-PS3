// custom_wallpapers.cpp - see custom_wallpapers.h.
#include "custom_wallpapers.h"
#include "twm.h"
#include "../../engine/gm.h"
#include "../../platform/gfx.h"
#include "../../platform/sys.h"
#include "../../platform/thread.h"
#include "../../platform/user_files.h"
#include <ctype.h>
#include <string.h>
#include <algorithm>
#include <map>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb_image.h"
#pragma GCC diagnostic pop

namespace twm {

static const char *const FULL_PREFIX = "the_world_machine/wallpaper/";
static const char *const THUMB_PREFIX = "the_world_machine/desktop_icons/wallpaper_thumbs/";
static const int THUMB_W = 84, THUMB_H = 48;
static const int MAX_PIXELS = 16 * 1000 * 1000;   // decoded that is 64 MB of memory: bigger pictures are refused

static std::vector<CustomWallpaper> g_list;
static std::map<std::string, std::vector<uint32_t>> g_thumbs;   // file -> 84x48 ARGB

static bool picture_ext(const std::string &n) {
	size_t d = n.find_last_of('.');
	if (d == std::string::npos) return false;
	std::string e = n.substr(d + 1);
	for (auto &c : e) c = (char)tolower((unsigned char)c);
	return e == "png" || e == "jpg" || e == "jpeg" || e == "bmp";
}

const std::vector<CustomWallpaper> &custom_wallpapers() { return g_list; }

std::string custom_wallpaper_name(const std::string &id) {
	for (auto &w : g_list)
		if (w.id == id) return w.displayName;
	return id.substr(7);
}

// Decodes the picture and scales it to cover w x h (cropping what overhangs,
// centred). Area averaging when shrinking, bilinear when enlarging.
static bool decode_cover(const std::string &file, int tw, int th, std::vector<uint32_t> &out) {
	std::vector<uint8_t> data;
	std::string path = sys::wallpaper_dir() + "/" + file;
	if (!sys::read_abs(path, data) || data.empty()) return false;
	int w, h, n;
	if (!stbi_info_from_memory(data.data(), (int)data.size(), &w, &h, &n) || w <= 0 || h <= 0 ||
	    (long long)w * h > MAX_PIXELS) {
		sys::log("wallpaper: %s isn't a picture this can read (or is too big)\n", file.c_str());
		return false;
	}
	uint64_t t0 = sys::time_us();
	unsigned char *px = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &n, 4);
	data.clear();
	data.shrink_to_fit();
	if (!px) {
		sys::log("wallpaper: %s: %s\n", file.c_str(), stbi_failure_reason());
		return false;
	}
	float s = std::max((float)tw / w, (float)th / h);   // source px per target px = 1/s
	float cw = tw / s, ch = th / s;                       // the part of the picture shown
	float ox = (w - cw) / 2, oy = (h - ch) / 2;
	out.assign((size_t)tw * th, 0xff000000u);
	for (int y = 0; y < th; y++) {
		for (int x = 0; x < tw; x++) {
			float sx0 = ox + x / s, sy0 = oy + y / s;
			unsigned r = 0, g = 0, b = 0, cnt = 0;
			if (s < 1.0f) {
				int x0 = (int)sx0, y0 = (int)sy0, x1 = (int)(sx0 + 1 / s), y1 = (int)(sy0 + 1 / s);
				if (x1 <= x0) x1 = x0 + 1;
				if (y1 <= y0) y1 = y0 + 1;
				x1 = std::min(x1, w);
				y1 = std::min(y1, h);
				for (int yy = y0; yy < y1; yy++) {
					const unsigned char *p = px + ((size_t)yy * w + x0) * 4;
					for (int xx = x0; xx < x1; xx++, p += 4) { r += p[0]; g += p[1]; b += p[2]; cnt++; }
				}
			} else {
				float fx = sx0 + 0.5f / s - 0.5f, fy = sy0 + 0.5f / s - 0.5f;
				int x0 = std::max(0, std::min(w - 1, (int)fx)), y0 = std::max(0, std::min(h - 1, (int)fy));
				int x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
				float ax = std::max(0.0f, std::min(1.0f, fx - x0)), ay = std::max(0.0f, std::min(1.0f, fy - y0));
				const unsigned char *p00 = px + ((size_t)y0 * w + x0) * 4, *p10 = px + ((size_t)y0 * w + x1) * 4;
				const unsigned char *p01 = px + ((size_t)y1 * w + x0) * 4, *p11 = px + ((size_t)y1 * w + x1) * 4;
				float c[3];
				for (int k = 0; k < 3; k++)
					c[k] = (p00[k] * (1 - ax) + p10[k] * ax) * (1 - ay) + (p01[k] * (1 - ax) + p11[k] * ax) * ay;
				r = (unsigned)c[0]; g = (unsigned)c[1]; b = (unsigned)c[2]; cnt = 1;
			}
			if (cnt) out[(size_t)y * tw + x] = 0xff000000u | (r / cnt) << 16 | (g / cnt) << 8 | (b / cnt);
		}
	}
	stbi_image_free(px);
	sys::log("wallpaper: %s (%dx%d) -> %dx%d in %.0f ms\n", file.c_str(), w, h, tw, th, (sys::time_us() - t0) / 1000.0);
	return true;
}

static const CustomWallpaper *find(const std::string &id) {
	for (auto &w : g_list)
		if (w.id == id) return &w;
	return nullptr;
}

// ------------------------------------------------------------------ the decoding thread
// Decoding and scaling a big JPEG takes the PPU a second or more, so it's
// done on a background thread; the desktop keeps drawing meanwhile.
struct Job { std::string key, file; int w, h; };
static sys::Monitor *g_mon;
static std::vector<Job> g_jobs;                                  // waiting
static std::map<std::string, std::vector<uint32_t>> g_done;      // key -> pixels (empty: failed)
static std::vector<std::string> g_queued;                        // keys waiting or being decoded
static std::vector<std::string> g_finished;                      // keys to tell gm about (main thread)
static bool g_threadOk, g_threadTried;

static void worker(void *) {
	for (;;) {
		Job j;
		{
			sys::MonitorLock l(g_mon);
			while (g_jobs.empty()) sys::monitor_wait(g_mon, 0);
			j = g_jobs.front();
			g_jobs.erase(g_jobs.begin());
		}
		std::vector<uint32_t> px;
		if (!decode_cover(j.file, j.w, j.h, px)) px.clear();
		sys::MonitorLock l(g_mon);
		g_done[j.key] = std::move(px);
		g_finished.push_back(j.key);
	}
}

// Queues the decode (once). False if it couldn't (no thread): decode now.
static bool request(const std::string &key, const std::string &file, int w, int h) {
	if (!g_threadTried) {
		g_threadTried = true;
		g_mon = sys::monitor_create("wallpapers");
		g_threadOk = g_mon && sys::thread_start(worker, nullptr, "wallpaper decode", -1);
		if (!g_threadOk) sys::log("wallpaper: no decoding thread, decoding on the main thread\n");
	}
	if (!g_threadOk) return false;
	sys::MonitorLock l(g_mon);
	if (std::find(g_queued.begin(), g_queued.end(), key) != g_queued.end()) return true;
	g_queued.push_back(key);
	g_jobs.push_back(Job{ key, file, w, h });
	sys::monitor_notify(g_mon);
	return true;
}

// Takes the decoded pixels for key if they're there.
static bool take(const std::string &key, std::vector<uint32_t> &px, bool *failed) {
	*failed = false;
	if (!g_threadOk) return false;
	sys::MonitorLock l(g_mon);
	auto it = g_done.find(key);
	if (it == g_done.end()) return false;
	px = std::move(it->second);
	*failed = px.empty();
	g_done.erase(it);
	g_queued.erase(std::remove(g_queued.begin(), g_queued.end(), key), g_queued.end());
	return true;
}

static std::string full_key(const std::string &id) { return std::string(FULL_PREFIX) + id; }

// gm's texture source: the full picture and its icon. Until the thread has
// decoded one, there's no texture (gm notes it missing; custom_wallpapers_poll
// clears that when the pixels arrive).
static gfx::Texture *make_texture(const std::string &name) {
	size_t fl = strlen(FULL_PREFIX), tl = strlen(THUMB_PREFIX);
	bool full = name.compare(0, fl, FULL_PREFIX) == 0 && is_custom_wallpaper(name.substr(fl));
	bool thumb = !full && name.compare(0, tl, THUMB_PREFIX) == 0 && is_custom_wallpaper(name.substr(tl));
	if (!full && !thumb) return nullptr;
	const CustomWallpaper *w = find(name.substr(full ? fl : tl));
	if (!w) return nullptr;
	if (thumb) {
		auto it = g_thumbs.find(w->file);
		if (it != g_thumbs.end()) return it->second.empty() ? nullptr : gfx::create_texture(THUMB_W, THUMB_H, it->second.data());
	}
	Vec2 sz = full ? space() : Vec2(THUMB_W, THUMB_H);   // full: one texel per desktop draw pixel
	std::vector<uint32_t> px;
	bool failed;
	if (!take(name, px, &failed)) {
		if (request(name, w->file, sz.x, sz.y)) return nullptr;   // on its way
		if (!decode_cover(w->file, sz.x, sz.y, px)) failed = true;
	}
	if (thumb) g_thumbs[w->file] = px;   // kept: the folder is opened again and again
	if (failed || px.empty()) return nullptr;
	return gfx::create_texture((uint32_t)sz.x, (uint32_t)sz.y, px.data());
}

void custom_wallpapers_poll() {
	if (!g_threadOk) return;
	std::vector<std::string> fin;
	{
		sys::MonitorLock l(g_mon);
		fin.swap(g_finished);
	}
	for (auto &k : fin) gm::forget_missing(k.c_str());   // asked for again next time it's drawn
}

bool custom_wallpaper_ready(const std::string &id) {
	const CustomWallpaper *w = find(id);
	if (!w) return true;   // nothing to wait for
	std::string key = full_key(id);
	if (!g_threadOk && g_threadTried) return true;
	Vec2 sz = space();
	if (!request(key, w->file, sz.x, sz.y)) return true;   // no thread: SetWallpaper decodes it
	sys::MonitorLock l(g_mon);
	return g_done.count(key) != 0;
}

void scan_custom_wallpapers() {
	static bool installed;
	if (!installed) {
		gm::set_texture_source(make_texture);
		installed = true;
	}
	g_list.clear();
	std::vector<std::string> files = sys::list_files(sys::wallpaper_dir());
	std::sort(files.begin(), files.end());
	for (auto &f : files) {
		if (!picture_ext(f)) continue;
		CustomWallpaper w;
		w.file = f;
		w.id = "custom:" + f;
		w.displayName = f.substr(0, f.find_last_of('.'));
		g_list.push_back(w);
	}
	sys::log("wallpaper: %d custom wallpapers in %s\n", (int)g_list.size(), sys::wallpaper_dir().c_str());
}

ImportResult import_custom_wallpapers() {
	ImportResult r;
	r.devices = sys::usb_device_count();
	std::vector<std::string> have = sys::list_files(sys::wallpaper_dir());
	for (auto &dir : sys::usb_wallpaper_dirs()) {
		r.sticks++;
		for (auto &f : sys::list_files(dir)) {
			if (!picture_ext(f)) continue;
			if (std::find(have.begin(), have.end(), f) != have.end()) { r.existing++; continue; }
			std::vector<uint8_t> data;
			int w, h, n;
			if (!sys::read_abs(dir + "/" + f, data) ||
			    !stbi_info_from_memory(data.data(), (int)data.size(), &w, &h, &n) || (long long)w * h > MAX_PIXELS) {
				r.unreadable++;
				sys::log("wallpaper: not importing %s/%s\n", dir.c_str(), f.c_str());
				continue;
			}
			if (!sys::write_abs(sys::wallpaper_dir() + "/" + f, data.data(), data.size())) {
				r.unreadable++;
				sys::log("wallpaper: couldn't write %s\n", f.c_str());
				continue;
			}
			have.push_back(f);
			r.imported++;
			sys::log("wallpaper: imported %s/%s (%dx%d)\n", dir.c_str(), f.c_str(), w, h);
		}
	}
	scan_custom_wallpapers();
	return r;
}

bool delete_custom_wallpaper(const std::string &id) {
	const CustomWallpaper *w = find(id);
	if (!w) return false;
	std::string file = w->file;
	bool ok = sys::remove_abs(sys::wallpaper_dir() + "/" + file);
	g_thumbs.erase(file);
	sys::log("wallpaper: deleted %s%s\n", file.c_str(), ok ? "" : " (couldn't remove the file)");
	scan_custom_wallpapers();
	return ok;
}

} // namespace twm
