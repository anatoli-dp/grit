#pragma once

#include "grit/compiler/grit_ir.h"

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

class GDScript;
class Resource;

class GritProjectCompiler {
public:
	struct Report {
		String fingerprint;
		int script_count = 0;
		int function_count = 0;
		int native_function_count = 0;
		int stripped_function_count = 0;
		HashMap<String, String> stripped_sources;
	};

private:
	struct ScriptSource {
		String path;
		String code;
		Ref<GDScript> script;

		bool operator<(const ScriptSource &p_other) const { return path < p_other.path; }
	};

	static void collect_files(const String &p_directory, Vector<String> &r_scripts, Vector<String> &r_resources);
	static void keep_exported(Vector<String> &r_paths, const HashSet<String> &p_exported_paths);
	static bool may_contain_builtin_scripts(const String &p_path);
	static void collect_builtin_scripts(const Variant &p_value, const String &p_owner_path, HashSet<const Object *> &r_visited, Vector<ScriptSource> &r_sources);
	static String compute_fingerprint(const Vector<ScriptSource> &p_sources, bool p_strip);
	static HashSet<String> strippable_functions(const LocalVector<GritFunction> &p_functions);
	static Error compile_script(const ScriptSource &p_source, LocalVector<GritFunction> &r_functions);

public:
	// p_exported_paths: the files the export ships (EditorExportPlatform::get_exported_paths), or null for the whole project.
	Error compile(const String &p_output_directory, bool p_strip, const HashSet<String> *p_exported_paths, Report &r_report);
};
