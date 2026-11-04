#include "grit_registry.h"

#include "grit_runtime_access.h"

#include "modules/gdscript/gdscript.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

GritRegistry *GritRegistry::singleton = nullptr;

String GritRegistry::make_class_path(const GDScript *p_script) {
	String chain;
	while (p_script && !p_script->is_root_script()) {
		chain = "::" + String(p_script->get_local_name()) + chain;
		p_script = GritRuntimeAccess::get_owner_script(p_script);
	}
	if (!p_script) {
		return String();
	}
	const String path = p_script->get_script_path();
	if (path.is_empty() || path.begins_with("gdscript://")) {
		return String();
	}
	return path + chain;
}

String GritRegistry::make_key(const String &p_script_path, const StringName &p_function_name, int p_line) {
	return vformat("%s::%s:%d", p_script_path, p_function_name, p_line);
}

String GritRegistry::make_lambda_key(const String &p_root_key, const StringName &p_function_name, int p_line, int p_ordinal) {
	return vformat("%s>%s:%d#%d", p_root_key, p_function_name, p_line, p_ordinal);
}

String GritRegistry::get_fingerprint_path() {
	return ProjectSettings::get_singleton()->get_project_data_path().path_join("grit").path_join("fingerprint");
}

String GritRegistry::make_pack_fingerprint(const String &p_fingerprint, bool p_is_stripped) {
	return p_is_stripped ? p_fingerprint + "\n" + STRIPPED_FLAG : p_fingerprint;
}

String GritRegistry::obfuscate_key(const String &p_key) {
	return p_key.sha256_text().substr(0, 24);
}

void GritRegistry::set_fingerprint(const String &p_fingerprint) {
	fingerprint = p_fingerprint;
}

void GritRegistry::add_function(const String &p_key, const String &p_signature, GDScriptFunction::NativeCall p_call, ConstantsFactory p_constants, bool p_is_stripped) {
	ERR_FAIL_NULL(p_call);
	if (ambiguous_keys.has(p_key)) {
		return;
	}
	if (functions.has(p_key)) {
		functions.erase(p_key);
		ambiguous_keys.insert(p_key);
		ERR_FAIL_MSG(vformat("Grit: duplicate native function \"%s\", using the GDScript VM for it.", p_key));
	}

	NativeFunction function;
	function.signature = p_signature;
	function.call = p_call;
	function.constants = p_constants;
	function.is_stripped = p_is_stripped;
	functions.insert(p_key, function);
}

// The editor gives a function without a return type the return type of the method it overrides
// (GDScriptAnalyzer::resolve_function_signature, TOOLS_ENABLED only), export templates keep it Variant.
static bool differs_only_by_inferred_return(const String &p_recorded, const String &p_runtime) {
	const String variant_return = " -> Variant";
	const int arrow = p_runtime.rfind(variant_return);
	if (arrow < 0) {
		return false;
	}
	const String head = p_runtime.substr(0, arrow + 4);
	const String tail = p_runtime.substr(arrow + variant_return.length());
	if (!tail.is_empty() && !tail.begins_with(" optional ")) {
		return false;
	}
	if (p_recorded.length() <= head.length() + tail.length() || !p_recorded.begins_with(head) || !p_recorded.ends_with(tail)) {
		return false;
	}
	const String recorded_return = p_recorded.substr(head.length(), p_recorded.length() - head.length() - tail.length());
	return !recorded_return.contains(" optional ");
}

const GritRegistry::NativeFunction *GritRegistry::find(const String &p_key, const String &p_signature) const {
	const String key = keys_obfuscated ? obfuscate_key(p_key) : p_key;
	HashMap<String, NativeFunction>::ConstIterator function = functions.find(key);
	if (!function) {
		return nullptr;
	}
	if (function->value.signature != p_signature && !differs_only_by_inferred_return(function->value.signature, p_signature)) {
		WARN_PRINT(vformat("Grit: signature of \"%s\" changed from \"%s\" to \"%s\", using the GDScript VM.", p_key, function->value.signature, p_signature));
		return nullptr;
	}
	attached_count.increment();
	return &function->value;
}

bool GritRegistry::is_stripped(const String &p_key) const {
	const NativeFunction *function = functions.getptr(keys_obfuscated ? obfuscate_key(p_key) : p_key);
	return function && function->is_stripped;
}

bool GritRegistry::matches_project() const {
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}

	Error error = OK;
	const PackedStringArray pack_lines = FileAccess::get_file_as_string(get_fingerprint_path(), &error).strip_edges().split("\n");
	const String pack_fingerprint = error == OK && !pack_lines.is_empty() ? pack_lines[0].strip_edges() : String();
#ifdef TOOLS_ENABLED
	const bool is_stripped_pack = false;
#else
	const bool is_stripped_pack = error == OK && pack_lines.has(STRIPPED_FLAG);
#endif
	if (functions.is_empty() && !is_stripped_pack) {
		return false;
	}

	if (OS::get_singleton()->get_environment("GRIT_DISABLE") == "1") {
		if (!is_stripped_pack) {
			print_line("Grit: native code disabled by GRIT_DISABLE.");
			return false;
		}
		ERR_PRINT("Grit: GRIT_DISABLE is ignored because the scripts of this game were stripped at export.");
	}

	if (pack_fingerprint.is_empty() || pack_fingerprint != fingerprint) {
		if (is_stripped_pack) {
			CRASH_NOW_MSG("Grit: the scripts of this game were stripped at export, but this executable does not contain their native code. Use the export template that was built together with this export.");
		}
		WARN_PRINT("Grit: native code was generated for different scripts, using the GDScript VM. Export the project again and rebuild the export template.");
		return false;
	}
	return true;
}

void GritRegistry::activate() {
#ifdef GRIT_CONFORMANCE
	active = !functions.is_empty();
#else
	active = matches_project();
#endif
	if (active) {
		print_verbose(vformat("Grit: %d native functions available.", functions.size()));
	}
	// TEMPORARY DEBUG (remove before the PR).
	print_line(vformat("Grit DEBUG: registry active=%s, %d native functions, keys obfuscated=%s.", active ? "yes" : "no", functions.size(), keys_obfuscated ? "yes" : "no"));
}

GritRegistry::GritRegistry() {
	singleton = this;
	grit_register_generated_functions(*this);
}

GritRegistry::~GritRegistry() {
	singleton = nullptr;
}
