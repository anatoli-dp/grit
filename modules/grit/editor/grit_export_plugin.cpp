#include "grit_export_plugin.h"

#include "grit_project_compiler.h"

#include "grit/runtime/grit_registry.h"

#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource.h"
#include "core/os/os.h"

static void write_export_platform(const Ref<EditorExportPreset> &p_preset) {
	const Ref<EditorExportPlatform> platform = p_preset->get_platform();
	List<String> features;
	platform->get_platform_features(&features);
	Vector<String> tags;
	for (const String &feature : features) {
		tags.push_back(feature);
	}
	const String architecture = p_preset->get("binary_format/architecture");

	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String path = settings->globalize_path(settings->get_project_data_path().path_join("grit").path_join("platform"));
	Error error = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	ERR_FAIL_COND_MSG(error != OK, vformat("Grit: could not create \"%s\".", path.get_base_dir()));
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &error);
	ERR_FAIL_COND_MSG(file.is_null(), vformat("Grit: could not write \"%s\".", path));
	file->store_string("platform_features=" + String(" ").join(tags) + "\narchitecture=" + architecture + "\n");
}

String GritExportPlugin::get_export_template(const Ref<EditorExportPreset> &p_preset, bool p_debug) {
	return OS::get_singleton()->get_environment(ENV_EXPORT_TEMPLATE);
}

void GritExportPlugin::_get_export_options(const Ref<EditorExportPlatform> &p_export_platform, List<EditorExportPlatform::ExportOption> *r_options) const {
	r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::BOOL, OPTION_STRIP_SCRIPTS), false));
}

void GritExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	stripped_sources.clear();
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

	const String output_directory = ProjectSettings::get_singleton()->globalize_path(GLOBAL_GET(SETTING_GENERATED_DIRECTORY));
	GritProjectCompiler compiler;
	GritProjectCompiler::Report report;
	const Error error = compiler.compile(output_directory, strip, report);
	if (error != OK) {
		ERR_PRINT("Grit: native code generation failed, the exported project will use the GDScript VM.");
		return;
	}

	if (preset.is_valid()) {
		write_export_platform(preset);
	}

	stripped_sources = report.stripped_sources;
	customization_hash = GritRegistry::make_pack_fingerprint(report.fingerprint, strip).hash64();
	add_file(GritRegistry::get_fingerprint_path(), GritRegistry::make_pack_fingerprint(report.fingerprint, strip).to_utf8_buffer(), false);
	print_line(vformat("Grit: generated C++ written to \"%s\". Rebuild the export template with grit_generated pointing there.", output_directory));
}

void GritExportPlugin::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	const String *source = stripped_sources.getptr(p_path);
	if (!source) {
		return;
	}
	if (script_mode == EditorExportPreset::MODE_SCRIPT_TEXT) {
		add_file(p_path, source->to_utf8_buffer(), false);
		skip();
		return;
	}
	const GDScriptTokenizerBuffer::CompressMode compress_mode = script_mode == EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED ? GDScriptTokenizerBuffer::COMPRESS_ZSTD : GDScriptTokenizerBuffer::COMPRESS_NONE;
	const Vector<uint8_t> tokens = GDScriptTokenizerBuffer::parse_code_string(*source, compress_mode);
	ERR_FAIL_COND_MSG(tokens.is_empty(), vformat("Grit: could not tokenize the stripped \"%s\".", p_path));
	add_file(p_path.get_basename() + ".gdc", tokens, true);
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
	stripped_sources.clear();
}
