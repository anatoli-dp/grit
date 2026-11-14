#pragma once

#include "grit/compiler/grit_ir.h"

#include "modules/gdscript/gdscript_parser.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

// The comments that keep code in the GDScript VM: "# grit: vm" on its own line among the comment and annotation lines
// directly above a func keeps that function and its lambdas there, and "# grit: vm-script" in the comment lines before
// the script's first line of code keeps every function of the script there.
class GritVMDirectives {
	using Comments = HashMap<int, GDScriptTokenizer::CommentData>;

	// The line of each function's func keyword, mapped to the line of its directive.
	HashMap<int, int> function_directives;
	int script_directive = 0;
	Vector<String> warnings;

	void collect_functions(const GDScriptParser::ClassNode *p_class, const Comments &p_comments, HashSet<int> &r_attached);

public:
	Error parse(const String &p_path, const String &p_source);
	const Vector<String> &get_warnings() const { return warnings; }
	// Sets the unsupported reason of each function a directive keeps in the VM.
	void apply(LocalVector<GritFunction> &r_functions) const;
};
