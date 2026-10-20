// xml.h - tiny DOM-style XML reader for Tiled .tmx / .tsx files.
// Handles elements, attributes, text, comments, <?xml?> and CDATA-free
// content with the five standard entities. Not a general XML parser.
#pragma once
#include <string>
#include <vector>
#include <memory>

namespace xml {

struct Node {
	std::string name;
	std::vector<std::pair<std::string, std::string>> attrs;
	std::vector<std::unique_ptr<Node>> children;
	std::string text;

	const char *attr(const char *key, const char *def = nullptr) const;
	int attr_int(const char *key, int def = 0) const;
	double attr_double(const char *key, double def = 0) const;
	const Node *child(const char *name) const;
};

// Returns the root element, or nullptr (and *err set) on malformed input.
std::unique_ptr<Node> parse(const char *text, std::string *err = nullptr);

} // namespace xml
