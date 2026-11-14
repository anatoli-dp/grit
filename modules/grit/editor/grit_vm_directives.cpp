#include "grit_vm_directives.h"

static constexpr const char *DIRECTIVE_PREFIX = "grit:";
static constexpr const char *FUNCTION_DIRECTIVE = "vm";
static constexpr const char *SCRIPT_DIRECTIVE = "vm-script";

static String get_directive(const String &p_comment) {
	if (!p_comment.begins_with("#") || p_comment.begins_with("##")) {
		return String();
	}
	const String text = p_comment.substr(1).strip_edges();
	if (!text.begins_with(DIRECTIVE_PREFIX)) {
		return String();
	}
	const Vector<String> words = text.trim_prefix(DIRECTIVE_PREFIX).split_spaces();
	return words.is_empty() ? String() : words[0];
}

void GritVMDirectives::collect_functions(const GDScriptParser::ClassNode *p_class, const Comments &p_comments, HashSet<int> &r_attached) {
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			collect_functions(member.m_class, p_comments, r_attached);
			continue;
		}
		if (member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
			continue;
		}
		const int function_line = member.function->start_line;
		int first_line = function_line;
		for (const GDScriptParser::AnnotationNode *annotation : member.function->annotations) {
			first_line = MIN(first_line, annotation->start_line);
		}
		for (int line = function_line - 1; line >= 1; line--) {
			const GDScriptTokenizer::CommentData *comment = p_comments.getptr(line);
			const bool is_own_line = comment && comment->new_line;
			if (line < first_line && !is_own_line) {
				break;
			}
			if (is_own_line && get_directive(comment->comment) == FUNCTION_DIRECTIVE) {
				if (!function_directives.has(function_line)) {
					function_directives.insert(function_line, line);
				}
				r_attached.insert(line);
			}
		}
	}
}

Error GritVMDirectives::parse(const String &p_path, const String &p_source) {
	function_directives.clear();
	script_directive = 0;
	warnings.clear();
	if (!p_source.contains(DIRECTIVE_PREFIX)) {
		return OK;
	}

	GDScriptParser parser;
	const Error error = parser.parse(p_source, p_path, false);
	if (error != OK) {
		return error;
	}
	const Comments &comments = parser.comment_data;
	HashSet<int> attached;
	collect_functions(parser.get_tree(), comments, attached);

	const Vector<String> lines = p_source.split("\n");
	int first_code_line = lines.size() + 1;
	for (int i = 0; i < lines.size(); i++) {
		const String text = lines[i].strip_edges();
		if (!text.is_empty() && !text.begins_with("#")) {
			first_code_line = i + 1;
			break;
		}
	}

	for (const KeyValue<int, GDScriptTokenizer::CommentData> &entry : comments) {
		const int line = entry.key;
		const String directive = get_directive(entry.value.comment);
		if (directive.is_empty()) {
			continue;
		}
		if (directive == SCRIPT_DIRECTIVE) {
			if (line < first_code_line) {
				if (script_directive == 0) {
					script_directive = line;
				}
			} else {
				warnings.push_back(vformat("line %d: \"# grit: %s\" is not above the script's first line of code, so it keeps nothing in the VM", line, directive));
			}
		} else if (directive == FUNCTION_DIRECTIVE) {
			if (!attached.has(line)) {
				warnings.push_back(vformat("line %d: \"# grit: %s\" is not on its own line directly above a function, so it keeps nothing in the VM", line, directive));
			}
		} else {
			warnings.push_back(vformat("line %d: \"# grit: %s\" is not a Grit directive, use \"# grit: %s\" or \"# grit: %s\"", line, directive, FUNCTION_DIRECTIVE, SCRIPT_DIRECTIVE));
		}
	}
	return OK;
}

void GritVMDirectives::apply(LocalVector<GritFunction> &r_functions) const {
	HashMap<String, String> function_reasons;
	for (const GritFunction &function : r_functions) {
		const int *directive_line = function_directives.getptr(function.initial_line);
		if (directive_line) {
			function_reasons.insert(function.registry_key, vformat("opted out (# grit: %s, line %d)", FUNCTION_DIRECTIVE, *directive_line));
		}
	}
	const String script_reason = script_directive > 0 ? vformat("opted out (# grit: %s, line %d)", SCRIPT_DIRECTIVE, script_directive) : String();
	for (GritFunction &function : r_functions) {
		const String *reason = function_reasons.getptr(function.registry_key.get_slicec('>', 0));
		if (reason) {
			function.unsupported_reason = *reason;
		} else if (!script_reason.is_empty()) {
			function.unsupported_reason = script_reason;
		}
	}
}
