#pragma once

#include <string>
#include <vector>
#include <algorithm>

struct char_range {
	char const* start;
	char const* end;

	std::string to_string() const {
		return std::string(start, end);
	}
};

struct parsed_item {
	char_range key;
	std::vector<char_range> values;
	char const* terminal;
};

struct error_record {
	std::string accumulated;

	void add(std::string const& s) {
		if(accumulated.length() > 0)
			accumulated += "\n";
		accumulated += s;
	}
};

char const* advance_to_closing_bracket(char const* pos, char const* end);
char const* advance_to_non_whitespace(char const* pos, char const* end);
char const* advance_to_whitespace(char const* pos, char const* end);
char const* advance_to_whitespace_or_brace(char const* pos, char const* end);
char const* reverse_to_non_whitespace(char const* start, char const* pos);
int32_t calculate_line_from_position(char const* start, char const* pos);
parsed_item extract_item(char const* input, char const* end, char const* global_start, error_record& err);


enum class storage_type { contiguous, erasable, compactable };
enum class property_type { vectorizable, other, object, special_vector, bitfield };

struct property_def {
	std::string name;

	property_type type = property_type::other;
	bool is_derived = false;

	int special_pool_size = 1000;
	std::string data_type;

	bool hook_get = false;
	bool hook_set = false;

	std::vector<std::string> property_tags;
};

enum class index_type { many, at_most_one, none };
enum class list_type { list, array, std_vector };

struct relationship_object_def;

struct related_object {
	std::string property_name;
	std::string type_name;
	index_type index;
	list_type ltype;
	relationship_object_def* related_to;
};

struct in_relation_information {
	std::string relation_name;
	std::string property_name;
	bool as_primary_key;
	index_type indexed_as;
	list_type listed_as;
	relationship_object_def* rel_ptr;
};

struct composite_index_def {
	std::string name;
	std::vector<std::string> component_indexes;
};

struct primary_key_type {
	std::string property_name;
	relationship_object_def* points_to = nullptr;

	bool operator==(related_object const& other) const {
		return points_to == other.related_to && other.property_name == property_name;
	}
	bool operator!=(related_object const& other) const {
		return points_to != other.related_to || other.property_name != property_name;
	}
};



struct relationship_object_def {
	std::string name;
	std::string force_pk;
	bool is_relationship = false;
	std::vector<std::string> obj_tags;
	size_t size = 1000;
	bool is_expandable = false;
	storage_type store_type = storage_type::contiguous;

	bool hook_create = false;
	bool hook_delete = false;
	bool hook_move = false;

	std::vector<related_object> indexed_objects;
	std::vector<property_def> properties;
	std::vector<in_relation_information> relationships_involved_in;
	std::vector<composite_index_def> composite_indexes;

	primary_key_type primary_key;
};

enum class filter_type { default_exclude, exclude, include };

struct load_save_def {
	std::string name;
	filter_type properties_filter = filter_type::default_exclude;
	filter_type objects_filter = filter_type::default_exclude;
	std::vector<std::string> obj_tags;
	std::vector<std::string> property_tags;
};

struct conversion_def {
	std::string from;
	std::string to;
};

struct file_def {
	std::string namspace = "dcon";
	std::vector<std::string> includes;
	std::vector<std::string> globals;
	std::vector<std::string> legacy_types;

	std::vector<relationship_object_def> relationship_objects;
	std::vector<load_save_def> load_save_routines;
	std::vector<conversion_def> conversion_list;

	std::vector<std::string> object_types;
};

related_object parse_link_def(char const* start, char const* end, char const* global_start, error_record& err_out);
property_def parse_property_def(char const* start, char const* end, char const* global_start, error_record& err_out);
relationship_object_def parse_relationship(char const* start, char const* end, char const* global_start, error_record& err_out);
relationship_object_def parse_object(char const* start, char const* end, char const* global_start, error_record& err_out);
std::vector<std::string> parse_legacy_types(char const* start, char const* end, char const* global_start, error_record& err_out);
conversion_def parse_conversion_def(char const* start, char const* end, char const* global_start, error_record& err_out);
file_def parse_file(char const* start, char const* end, error_record& err_out);
load_save_def parse_load_save_def(char const* start, char const* end, char const* global_start, error_record& err_out);

inline relationship_object_def* find_by_name(file_def& def, std::string const& name) {
	if(auto r = std::find_if(
		def.relationship_objects.begin(), def.relationship_objects.end(),
		[&name](relationship_object_def const& o) { return o.name == name; }); r != def.relationship_objects.end()) {
		return &(*r);
	}
	return nullptr;
}

inline std::string make_relationship_parameters(relationship_object_def const& o) {
	std::string result;
	for(auto& i : o.indexed_objects) {
		if(result.length() != 0)
			result += ", ";
		result += i.type_name + "_id " + i.property_name + "_p";
	}
	return result;
}

inline bool is_vectorizable_type(file_def& def, std::string const& name) {
	return name == "char" || name == "int" || name == "short" || name == "unsigned short" ||
		name == "unsigned int" || name == "unsigned char" || name == "signed char" ||
		name == "float" || name == "int8_t" || name == "uint8_t" ||
		name == "int16_t" || name == "uint16_t" || name == "int32_t" || name == "uint32_t"
		|| [&]() {
		return name.length() >= 4 && name[name.length() - 1] == 'd' && name[name.length() - 2] == 'i' &&
			name[name.length() - 3] == '_' && find_by_name(def, name.substr(0, name.length() - 3)) != nullptr;
	}();
}

inline std::vector<std::string> common_types{ std::string("int8_t"), std::string("uint8_t"), std::string("int16_t"), std::string("uint16_t")
	, std::string("int32_t"), std::string("uint32_t"), std::string("int64_t"), std::string("uint64_t"), std::string("float"), std::string("double") };

inline std::string normalize_type(std::string in) {
	if(in == "char" || in == "unsigned char")
		return "uint8_t";
	if(in == "signed char")
		return "int8_t";
	if(in == "short")
		return "int16_t";
	if(in == "unsigned short")
		return "uint16_t";
	if(in == "int" || in == "long")
		return "int32_t";
	if(in == "unsigned int" || in == "unsigned long")
		return "uint32_t";
	if(in == "size_t" || in == "unsigned long long")
		return "uint64_t";
	if(in == "long long")
		return "int64_t";
	else
		return in;
}

inline bool is_common_type(std::string in) {
	return std::find(common_types.begin(), common_types.end(), normalize_type(in)) != common_types.end();
}

inline std::string size_to_tag_type(size_t sz) {
	if(sz == 0) {
		return "uint32_t";
	} else if(sz <= 126) {
		return "uint8_t";
	} else if(sz <= std::numeric_limits<int16_t>::max() - 1) {
		return "uint16_t";
	}
	return "uint32_t";
}