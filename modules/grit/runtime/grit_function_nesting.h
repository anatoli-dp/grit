#pragma once

#include "grit_registry.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

template <typename Payload>
class GritFunctionNesting {
	struct Entry {
		StringName name;
		int line = 0;
		bool debug_only = false;
		Payload payload;
	};

	static inline thread_local GritFunctionNesting *current = nullptr;

	GritFunctionNesting *previous = nullptr;
	GritFunctionNesting *parent = nullptr;
	GritFunctionNesting *root = this;
	LocalVector<Entry> entries;
	uint32_t debug_mark = 0;

public:
	// The compiler says which functions are lambdas (GDScriptCodeGenerator::set_lambda). A lambda is compiled
	// inside its enclosing function, so that function's generator is the innermost open one. Any other function
	// is a root, also when the compiler compiles a script it depends on in the middle of another function.
	void set_lambda(bool p_lambda) {
		if (p_lambda && previous) {
			parent = previous;
			root = parent->root;
		}
	}

	bool is_nested() const { return parent != nullptr; }

	void add(const StringName &p_name, int p_line, Payload &&p_payload) {
		Entry entry;
		entry.name = p_name;
		entry.line = p_line;
		entry.payload = std::move(p_payload);
		root->entries.push_back(std::move(entry));
	}

	void mark_newline() {
		debug_mark = root->entries.size();
	}

	void mark_assert() {
		for (uint32_t i = debug_mark; i < root->entries.size(); i++) {
			root->entries[i].debug_only = true;
		}
	}

	template <typename Callback>
	void resolve(const String &p_root_key, Callback p_callback) {
		HashMap<String, int> ordinals;
		for (Entry &entry : entries) {
			if (entry.debug_only) {
				continue;
			}
			const String local_key = vformat("%s:%d", entry.name, entry.line);
			int *ordinal = ordinals.getptr(local_key);
			if (!ordinal) {
				ordinal = &ordinals.insert(local_key, 0)->value;
			}
			p_callback(GritRegistry::make_lambda_key(p_root_key, entry.name, entry.line, *ordinal), entry.payload);
			(*ordinal)++;
		}
		entries.clear();
	}

	GritFunctionNesting() {
		previous = current;
		current = this;
	}

	~GritFunctionNesting() {
		current = previous;
	}

	GritFunctionNesting(const GritFunctionNesting &) = delete;
	GritFunctionNesting &operator=(const GritFunctionNesting &) = delete;
};
