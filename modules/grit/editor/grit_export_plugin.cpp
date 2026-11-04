#include "grit_export_plugin.h"

#include "grit_project_compiler.h"

#include "grit/runtime/grit_registry.h"

#include "modules/gdscript/gdscript.h"

#include "core/config/project_settings.h"
#include "core/io/resource.h"

static const HashMap<String, String> *exporting_sources = nullptr;

String GritExportPlugin::get_export_source(const String &p_path, const String &p_source) {
	const String *source = exporting_sources ? exporting_sources->getptr(p_path) : nullptr;
	return source ? *source : p_source;
}

void GritExportPlugin::_get_export_options(const Ref<EditorExportPlatform> &p_export_platform, List<EditorExportPlatform::ExportOption> *r_options) const {
	r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::BOOL, OPTION_STRIP_SCRIPTS), false));
}

void GritExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	stripped_sources.clear();
	exporting_sources = nullptr;
	if (!bool(GLOBAL_GET(SETTING_ENABLED))) {
		return;
	}

	const Ref<EditorExportPreset> preset = get_export_preset();
	script_mode = preset.is_valid() ? preset->get_script_export_mode() : EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED;
	const bool strip_requested = bool(get_option(OPTION_STRIP_SCRIPTS));
	const bool strip = strip_requested && !p_debug;
	if (strip_requested && p_debug) {
		print_line("Grit: scripts are only stripped in release exports, this debug export keeps them.");
	}

	// Compile only what this export ships, as Godot itself decides it from the preset.
	const Ref<EditorExportPlatform> platform = preset.is_valid() ? preset->get_platform() : Ref<EditorExportPlatform>();
	HashSet<String> exported_paths;
	if (platform.is_valid()) {
		exported_paths = platform->get_exported_paths(preset);
	}

	const String output_directory = ProjectSettings::get_singleton()->globalize_path(GLOBAL_GET(SETTING_GENERATED_DIRECTORY));
	GritProjectCompiler compiler;
	GritProjectCompiler::Report report;
	const Error error = compiler.compile(output_directory, strip, platform.is_valid() ? &exported_paths : nullptr, report);
	if (error != OK) {
		ERR_PRINT("Grit: native code generation failed, the exported project will use the GDScript VM.");
		return;
	}

	stripped_sources = report.stripped_sources;
	exporting_sources = &stripped_sources;
	customization_hash = GritRegistry::make_pack_fingerprint(report.fingerprint, strip).hash64();
	add_file(GritRegistry::get_fingerprint_path(), GritRegistry::make_pack_fingerprint(report.fingerprint, strip).to_utf8_buffer(), false);
	print_line(vformat("Grit: generated C++ written to \"%s\". Rebuild the export template with grit_generated pointing there.", output_directory));
}

void GritExportPlugin::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	// Binary tokens are written by the GDScript exporter, which takes the stripped source from get_export_source().
	// Export plugins run sorted by name, so this plugin cannot run before it.
	if (script_mode != EditorExportPreset::MODE_SCRIPT_TEXT) {
		return;
	}
	const String *source = stripped_sources.getptr(p_path);
	if (!source) {
		return;
	}
	add_file(p_path, source->to_utf8_buffer(), false);
	skip();
}

bool GritExportPlugin::_begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) {
	for (const KeyValue<String, String> &entry : stripped_sources) {
		if (entry.key.contains("::")) {
			return true;
		}
	}
	return false;
}

Ref<Resource> GritExportPlugin::_customize_resource(const Ref<Resource> &p_resource, const String &p_path) {
	const Ref<GDScript> script = p_resource;
	if (script.is_null() || !script->is_built_in()) {
		return Ref<Resource>();
	}
	const String *source = stripped_sources.getptr(script->get_path());
	if (!source || ResourceCache::get_ref(script->get_path()).ptr() == script.ptr()) {
		return Ref<Resource>();
	}
	script->set_source_code(*source);
	return script;
}

uint64_t GritExportPlugin::_get_customization_configuration_hash() const {
	return customization_hash;
}

void GritExportPlugin::_export_end() {
	exporting_sources = nullptr;
	stripped_sources.clear();
}
