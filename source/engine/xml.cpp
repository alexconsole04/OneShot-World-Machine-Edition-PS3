#include "xml.h"
#include <stdlib.h>
#include <string.h>

namespace xml {

const char *Node::attr(const char *key, const char *def) const {
	for (auto &a : attrs)
		if (a.first == key) return a.second.c_str();
	return def;
}
int Node::attr_int(const char *key, int def) const {
	const char *v = attr(key);
	return v ? atoi(v) : def;
}
double Node::attr_double(const char *key, double def) const {
	const char *v = attr(key);
	return v ? strtod(v, nullptr) : def;
}
const Node *Node::child(const char *n) const {
	for (auto &c : children)
		if (c->name == n) return c.get();
	return nullptr;
}

namespace {

struct Parser {
	const char *p;
	std::string err;

	static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
	static bool is_name(char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		       c == '_' || c == '-' || c == ':' || c == '.';
	}
	void ws() { while (is_space(*p)) p++; }

	static void decode(std::string &out, const char *s, const char *e) {
		while (s < e) {
			if (*s == '&') {
				struct { const char *ent; char ch; } ents[] = {
					{ "&lt;", '<' }, { "&gt;", '>' }, { "&amp;", '&' }, { "&quot;", '"' }, { "&apos;", '\'' } };
				bool hit = false;
				for (auto &x : ents) {
					size_t n = strlen(x.ent);
					if ((size_t)(e - s) >= n && memcmp(s, x.ent, n) == 0) { out += x.ch; s += n; hit = true; break; }
				}
				if (hit) continue;
				if (s[1] == '#') {   // numeric entity, ASCII range only
					const char *q = s + 2;
					long v = (*q == 'x') ? strtol(q + 1, (char **)&q, 16) : strtol(q, (char **)&q, 10);
					if (*q == ';') { if (v < 128) out += (char)v; else out += '?'; s = q + 1; continue; }
				}
			}
			out += *s++;
		}
	}

	bool skip_misc() {   // comments, <?..?>, <!DOCTYPE>
		for (;;) {
			ws();
			if (strncmp(p, "<?", 2) == 0) {
				const char *e = strstr(p, "?>");
				if (!e) { err = "unterminated <?"; return false; }
				p = e + 2;
			} else if (strncmp(p, "<!--", 4) == 0) {
				const char *e = strstr(p, "-->");
				if (!e) { err = "unterminated comment"; return false; }
				p = e + 3;
			} else if (strncmp(p, "<!", 2) == 0) {
				const char *e = strchr(p, '>');
				if (!e) { err = "unterminated <!"; return false; }
				p = e + 1;
			} else {
				return true;
			}
		}
	}

	std::unique_ptr<Node> element() {
		if (*p != '<') { err = "expected <"; return nullptr; }
		p++;
		auto n = std::make_unique<Node>();
		const char *s = p;
		while (is_name(*p)) p++;
		n->name.assign(s, p);
		for (;;) {
			ws();
			if (*p == '/' && p[1] == '>') { p += 2; return n; }
			if (*p == '>') { p++; break; }
			if (!is_name(*p)) { err = "bad attribute in <" + n->name + ">"; return nullptr; }
			s = p;
			while (is_name(*p)) p++;
			std::string key(s, p);
			ws();
			if (*p != '=') { err = "expected ="; return nullptr; }
			p++;
			ws();
			char q = *p;
			if (q != '"' && q != '\'') { err = "expected quote"; return nullptr; }
			p++;
			s = p;
			while (*p && *p != q) p++;
			if (!*p) { err = "unterminated attribute"; return nullptr; }
			std::string val;
			decode(val, s, p);
			p++;
			n->attrs.emplace_back(std::move(key), std::move(val));
		}
		// content
		for (;;) {
			s = p;
			while (*p && *p != '<') p++;
			if (!*p) { err = "unexpected end in <" + n->name + ">"; return nullptr; }
			decode(n->text, s, p);
			if (strncmp(p, "</", 2) == 0) {
				p += 2;
				while (is_name(*p)) p++;
				ws();
				if (*p != '>') { err = "bad close tag"; return nullptr; }
				p++;
				return n;
			}
			if (strncmp(p, "<!--", 4) == 0 || strncmp(p, "<?", 2) == 0) {
				if (!skip_misc()) return nullptr;
				continue;
			}
			auto c = element();
			if (!c) return nullptr;
			n->children.push_back(std::move(c));
		}
	}
};

} // namespace

std::unique_ptr<Node> parse(const char *text, std::string *err) {
	Parser ps{ text, {} };
	if (!ps.skip_misc()) { if (err) *err = ps.err; return nullptr; }
	auto root = ps.element();
	if (!root && err) *err = ps.err;
	return root;
}

} // namespace xml
