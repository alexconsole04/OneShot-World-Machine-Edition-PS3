#include "event_data.h"
#include "../platform/sys.h"
#include "cJSON.h"
#include <stdlib.h>

EventFile::~EventFile() { if (root) cJSON_Delete(root); }

static int jint(const cJSON *o, const char *k, int def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(v)) return v->valueint;
	if (cJSON_IsString(v)) return atoi(v->valuestring);
	return def;
}
static bool jbool(const cJSON *o, const char *k, bool def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
	return def;
}
static std::string jstr(const cJSON *o, const char *k) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
}

static void parse_route(const cJSON *r, MoveRoute &mr) {
	if (!r) return;
	mr.repeat = jbool(r, "repeat", false);
	mr.skippable = jbool(r, "skippable", false);
	const cJSON *list = cJSON_GetObjectItemCaseSensitive(r, "list");
	const cJSON *c;
	cJSON_ArrayForEach(c, list) {
		MoveCommand mc;
		mc.code = jint(c, "code", 0);
		const cJSON *ps = cJSON_GetObjectItemCaseSensitive(c, "parameters"), *pp;
		cJSON_ArrayForEach(pp, ps) {
			if (cJSON_IsString(pp)) mc.parameters.push_back(pp->valuestring);
			else if (cJSON_IsNumber(pp)) mc.parameters.push_back(std::to_string(pp->valueint));
			else mc.parameters.push_back(std::string());
		}
		mr.list.push_back(std::move(mc));
	}
}

std::unique_ptr<EventFile> load_event_file(const char *rel_path) {
	uint8_t *data;
	size_t size;
	if (!sys::read_file(rel_path, &data, &size)) {
		sys::log("events: cannot read %s\n", rel_path);
		return nullptr;
	}
	auto ef = std::make_unique<EventFile>();
	ef->root = cJSON_ParseWithLength((const char *)data, size);
	free(data);
	if (!ef->root) {
		sys::log("events: bad JSON in %s\n", rel_path);
		return nullptr;
	}
	const cJSON *events = cJSON_GetObjectItemCaseSensitive(ef->root, "events"), *e;
	cJSON_ArrayForEach(e, events) {
		EventData ed;
		ed.id = jint(e, "id", 0);
		ed.name = jstr(e, "name");
		ed.x = jint(e, "x", 0);
		ed.y = jint(e, "y", 0);
		const cJSON *pages = cJSON_GetObjectItemCaseSensitive(e, "pages"), *pg;
		cJSON_ArrayForEach(pg, pages) {
			EventPage p;
			p.move_type = jint(pg, "move_type", 0);
			p.move_speed = jint(pg, "move_speed", 2);
			p.move_frequency = jint(pg, "move_frequency", 2);
			p.walk_anime = jbool(pg, "walk_anime", false);
			p.step_anime = jbool(pg, "step_anime", false);
			p.direction_fix = jbool(pg, "direction_fix", false);
			p.through = jbool(pg, "through", false);
			p.always_on_top = jbool(pg, "always_on_top", false);
			p.always_on_bottom = jbool(pg, "always_on_bottom", false);
			p.trigger = jint(pg, "trigger", 0);
			const cJSON *c = cJSON_GetObjectItemCaseSensitive(pg, "condition");
			if (c) {
				p.condition.switch1_valid = jbool(c, "switch1_valid", false);
				p.condition.switch2_valid = jbool(c, "switch2_valid", false);
				p.condition.variable_valid = jbool(c, "variable_valid", false);
				p.condition.self_switch_valid = jbool(c, "self_switch_valid", false);
				p.condition.switch1_id = jint(c, "switch1_id", 1);
				p.condition.switch2_id = jint(c, "switch2_id", 1);
				p.condition.variable_id = jint(c, "variable_id", 1);
				p.condition.variable_value = jint(c, "variable_value", 0);
				p.condition.self_switch_ch = jstr(c, "self_switch_ch");
			}
			const cJSON *g = cJSON_GetObjectItemCaseSensitive(pg, "graphic");
			if (g) {
				p.graphic.tile_id = jint(g, "tile_id", 0);
				p.graphic.character_name = jstr(g, "character_name");
				p.graphic.character_hue = jint(g, "character_hue", 0);
				p.graphic.direction = jint(g, "direction", 2);
				p.graphic.pattern = jint(g, "pattern", 0);
				p.graphic.opacity = jint(g, "opacity", 255);
				p.graphic.blend_type = jint(g, "blend_type", 0);
			}
			parse_route(cJSON_GetObjectItemCaseSensitive(pg, "move_route"), p.move_route);
			p.list = cJSON_GetObjectItemCaseSensitive(pg, "list");
			ed.pages.push_back(std::move(p));
		}
		ef->events.push_back(std::move(ed));
	}
	return ef;
}
