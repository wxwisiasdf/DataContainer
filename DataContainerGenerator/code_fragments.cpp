#include "code_fragments.hpp"

basic_builder& make_load_record(basic_builder& o, file_def const & fd) {
	o + "struct load_record" + class_block{
		for(auto& ro : fd.relationship_objects) {
			o + substitute("object", ro.name);
			o + "bool @object@ = false;";
			for(auto& io : ro.indexed_objects) {
				o + substitute("prop_name", io.property_name);
				o + "bool @object@_@prop_name@ = false;";
			}
			for(auto& io : ro.properties) {
				o + substitute("prop_name", io.name);
				o + "bool @object@_@prop_name@ = false;";
			}
		}
	};
	return o;
}

basic_builder& make_id_definition(basic_builder& o, std::string const& type_name, std::string const& underlying_type) {
	o + substitute("name", type_name) + substitute("type", underlying_type);
	o + heading{"definition of strongly typed index for @name@"};
	o + "class @name@_id" + class_block{ o
		+ "public:"
		+ "using value_base_t = @type@;"
		+ "using zero_is_null_t = std::true_type;"
		+ line_break{}
		+"@type@ value;"
		+ line_break{}
		+"explicit constexpr @name@_id(@type@ v) noexcept : value(v + 1) {}"
		+ "constexpr @name@_id(@name@_id const& v) noexcept = default;"
		+ "constexpr @name@_id(@name@_id&& v) noexcept = default;"
		+ "constexpr @name@_id() noexcept : value(@type@(0)) {}"
		+ line_break{}
		+"@name@_id& operator=(@name@_id v) noexcept { value = v.value; return *this; }"
		+"constexpr bool operator==(@name@_id v) const noexcept { return value == v.value; }"
		+ "constexpr bool operator!=(@name@_id v) const noexcept { return value != v.value; }"
		+ "explicit constexpr operator bool() const noexcept { return value != @type@(0); }"
		+ "constexpr DCON_RELEASE_INLINE int32_t index() const noexcept" + block{
			o + "return int32_t(value) - 1;";
		};
	} + line_break{}
	+"class @name@_id_pair" + class_block{ o
		+ "public:"
		+ "@name@_id left;"
		+ "@name@_id right;";
	} + line_break{}
	+ "DCON_RELEASE_INLINE bool is_valid_index(@name@_id id) { return bool(id); }" + line_break{};

	return o;
}

basic_builder& make_value_to_vector_type(basic_builder& o, std::string const& qualified_name) {
	o + substitute("type", qualified_name)
		+ "template<>"
		+ "struct value_to_vector_type_s<@type@>" + class_block {
			o + "using type = tagged_vector<@type@>;";
		} + line_break{};
	return o;
}

basic_builder& make_member_container(basic_builder& o,
	std::string const& member_name, std::string const& type_name, std::string const& size,
	struct_padding pad, bool is_expandable, std::optional<std::string> const& special_fill) {

	o + substitute("type", type_name) + substitute("name", member_name) + substitute{"size", size}
	+ substitute{ "align", pad != struct_padding::none ? "alignas(64) " : "" }
	+ substitute{"fill", special_fill.has_value() ? *special_fill : std::string("")};

	o + heading{"storage space for @name@ of type @type@"};
	o + "struct @align@dtype_@name@" + block{
		if(is_expandable) {
			o + "std::vector<@type@> values;"
			+ "DCON_RELEASE_INLINE auto vptr() const { return values.data() + 1; }"
			+ "DCON_RELEASE_INLINE auto vptr() { return values.data() + 1; }";
		} else {
			if(pad == struct_padding::fixed)
				o + "uint8_t padding[64];";
			else if(pad == struct_padding::calculated)
				o + "uint8_t padding[(sizeof(@type@) + 63ui32) & ~63ui32];";
			o + "@type@ values[@size@];"
			+ "DCON_RELEASE_INLINE auto vptr() const { return values; }"
			+ "DCON_RELEASE_INLINE auto vptr() { return values; }";
		}
		
		if(!special_fill.has_value()) {
			if(is_expandable)
				o + "dtype_@name@() { values.emplace_back(); }";
			else if(pad == struct_padding::none)
				o + "dtype_@name@() { std::uninitialized_value_construct_n(values, @size@); }";
			else
				o + "dtype_@name@() { std::uninitialized_value_construct_n(values - 1, 1 + @size@); }";
		} else {
			if(is_expandable)
				o + "dtype_@name@() { values.push_back( @fill@ ); }";
			else if(pad == struct_padding::none)
				o + "dtype_@name@() { std::uninitialized_fill_n(values, @size@, @fill@); }";
			else
				o + "dtype_@name@() { std::uninitialized_fill_n(values - 1, 1 + @size@, @fill@); }";
		}
	} +"m_@name@;" + line_break{};

	return o;
}

std::string expand_size_to_fill_cacheline_calculation(std::string const& member_type, size_t base_size) {
	return std::string("(sizeof(") + member_type + ") <= 64 ? ("
		"uint32_t(" + std::to_string(base_size) + ") + (64ui32 / uint32_t(sizeof(" + member_type + "))) - 1ui32) "
		"& ~(64ui32 / uint32_t(sizeof(" + member_type + ")) - 1ui32) : uint32_t(" + std::to_string(base_size) + "))";
}

basic_builder& make_erasable_object_constructor(basic_builder& o, std::string const& name, size_t size) {
	o + substitute{ "name", name } +substitute{ "size", std::to_string(size) } +substitute{"u_type", size_to_tag_type(size) }
	+"@name@" + block{
		o + "for(int32_t i = @size@ - 1; i >= 0; --i)" + block{
			o + "m__index.vptr()[i] = first_free;";
			o + "first_free = @name@_id(@u_type@(i));";
		};
	};
	return o;
}

basic_builder& make_hooked_getters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& property_type, hook_type ht, bool is_expandable) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name } +substitute{ "type", property_type } +
		substitute{ "vector_position", is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" };

	o + heading{"getter hooks for @obj@: @prop@"};
	
	if(ht == hook_type::bitfield) {
		o + "bool @obj@_get_@prop@(@obj@_id id) const;";
		o + "ve::vbitfield_type @obj@_get_@prop@(@vector_position@<@obj@_id> id) const" + block{
			o + "ve::vbitfield_type r; r.v = 0;";
			o + "for(int32_t i = 0; i < ve::vector_size; ++i)" + block{
				o + "tr.v |= (@obj@_get_@prop@(@@obj_id(id.value + i))) << i;";
			};
			o + "return r;";
		};
		o + "ve::vbitfield_type @obj@_get_@prop@(ve::partial_contiguous_tags<@obj@_id> id) const" + block{
			o + "ve::vbitfield_type r; r.v = 0;";
			o + "for(int32_t i = 0; i < id.subcount; ++i)" + block{
				o + "tr.v |= (@obj@_get_@prop@(@@obj_id(id.value + i))) << i;";
			};
			o + "return r;";
		};
		o + "ve::vbitfield_type @obj@_get_@prop@(ve::tagged_vector<@obj@ _id> id) const" + block{
			o + "ve::vbitfield_type r; r.v = 0;";
			o + "for(int32_t i = 0; i < ve::vector_size; ++i)" + block{
				o + "tr.v |= (@obj@_get_@prop@(@@obj_id(id.value + i))) << i;";
			};
			o + "return r;";
		};
	} else if(ht == hook_type::vectorizable) {
		o + "@type@ @obj@_get_@prop@(@obj@_id id) const;";
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@(@vector_position@<@obj@_id> id) const" + block{
			o + "return ve::apply([t = this](@obj@_id i){ return t->@obj@_get_@prop@(i); }, id);";
		};
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@(ve::partial_contiguous_tags<@obj@_id> id) const" + block{
			o + "return ve::apply([t = this](@obj@_id i){ return t->@obj@_get_@prop@(i); }, id);";
		};
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@(ve::tagged_vector<@obj@_id> id) const" + block{
			o + "return ve::apply([t = this](@obj@_id i){ return t->@obj@_get_@prop@(i); }, id);";
		};
	} else {
		o + "@type@ @obj@_get_@prop@(@obj@_id id) const;";
	}
	o + line_break{};
	return o;
}

basic_builder& make_hooked_setters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& property_type, hook_type ht, bool is_expandable) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name } +substitute{ "type", property_type } +
		substitute{ "vector_position", is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" };
	o + heading{ "setter hooks for @obj@: @prop@" };

	if(ht == hook_type::bitfield) {
		o + "void @obj@_set_@prop@(@obj@_id id, bool value);";
		o + "void @obj@_set_@prop@(@vector_position@<@obj@_id> id, ve::vbitfield_type value)" + block{
			o + "for(int32_t i = 0; i < ve::vector_size; ++i)" + block{
				o + "@obj@_set_@prop@(@obj@_id(id.value + i), (value.v & uint8_t(1 << i)) != 0);";
			};
		};
		o + "void @obj@_set_@prop@(partial_contiguous_tags<@obj@_id> id, ve::vbitfield_type value)" + block{
			o + "for(int32_t i = 0; i < id.subcount; ++i)" + block{
				o + "@obj@_set_@prop@(@obj@_id(id.value + i), (value.v & uint8_t(1 << i)) != 0);";
			};
		};
		o + "void @obj@_set_@prop@(ve::tagged_vector<<@obj@_id> id, ve::vbitfield_type value)" + block{
			o + "for(int32_t i = 0; i < ve::vector_size; ++i)" + block{
				o + "@obj@_set_@prop@(id[i], (value.v & uint8_t(1 << i)) != 0);";
			};
		};
	} else if(ht == hook_type::vectorizable) {
		o + "void @obj@_set_@prop@(@obj@_id id, @type@ value);";
		o + "void @obj@_set_@prop@(@vector_position@<@obj@_id> id, ve::value_to_vector_type<@type@> value)" + block{
			o + "ve::apply(value, [t = this](@obj@_id i, @type@ v){ t->@obj@_set_@prop@(i, v); }, id);";
		};
		o + "void @obj@_set_@prop@(partial_contiguous_tags<@obj@_id> id, ve::value_to_vector_type<@type@> value)" + block{
			o + "ve::apply(value, [t = this](@obj@_id i, @type@ v){ t->@obj@_set_@prop@(i, v); }, id);";
		};
		o + "void @obj@_set_@prop@(ve::tagged_vector<@obj@_id> id, ve::value_to_vector_type<@type@> value)" + block{
			o + "ve::apply(value, [t = this](@obj@_id i, @type@ v){ t->@obj@_set_@prop@(i, v); }, id);";
		};
	} else {
		o + "void @obj@_set_@prop@(@obj@_id id, @type@ const& value);";
	}

	o + line_break{};
	return o;
}

basic_builder& make_vectorizable_getters(basic_builder& o, std::string const& type, bool is_expandable) {
	o + substitute{ "vector_position", is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" }
		+ substitute{"type", type};
	o + "DCON_RELEASE_INLINE @type@ @obj@_get_@prop@(@vector_position@<@obj@_id> id) const noexcept" + block{
		o + "return ve::load(id, @obj@.m_@prop@.vptr());";
	};
	o + "DCON_RELEASE_INLINE @type@ @obj@_get_@prop@(ve::partial_contiguous_tags<@obj@_id> id) const noexcept" + block{
		o + "return ve::load(id, @obj@.m_@prop@.vptr());";
	};
	o + "DCON_RELEASE_INLINE @type@ @obj@_get_@prop@(ve::tagged_vector<@obj@_id> id) const noexcept" + block{
		o + "return ve::load(id, @obj@.m_@prop@.vptr());";
	};

	return o;
}

basic_builder& make_vectorizable_setters(basic_builder& o, std::string const& type, bool is_expandable) {
	o + substitute{ "vector_position", is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" }
	+substitute{ "type", type };

	o + "DCON_RELEASE_INLINE void @obj@_set_@prop@(@vector_position@<@obj@_id> id, @type@ values) noexcept" + block{
		o + "ve::store(id, @obj@.m_@prop@.vptr(), values);";
	};
	o + "DCON_RELEASE_INLINE void @obj@_set_@prop@(ve::partial_contiguous_tags<@obj@_id> id, @type@ values) noexcept" + block{
		o + "ve::store(id, @obj@.m_@prop@.vptr(), values);";
	};
	o + "DCON_RELEASE_INLINE void @obj@_set_@prop@(ve::tagged_vector<@obj@_id> id, @type@ values) noexcept" + block{
		o + "ve::store(id, @obj@.m_@prop@.vptr(), values);";
	};

	return o;
}

basic_builder& make_single_getter(basic_builder& o, std::string const& type, bool return_reference) {
	o + substitute{ "type", type };
	if(!return_reference) {
		o + "DCON_RELEASE_INLINE @type@ @obj@_get_@prop@(@obj@_id id) const noexcept" + block{
			o + "return @obj@.m_@prop@.vptr()[id.index()];";
		};
	} else {
		o + "DCON_RELEASE_INLINE @type@ const& @obj@_get_@prop@(@obj@_id id) const noexcept" + block{
			o + "return @obj@.m_@prop@.vptr()[id.index()];";
		};
		o + "DCON_RELEASE_INLINE @type@& @obj@_get_@prop@(@obj@_id id) noexcept" + block{
			o + "return @obj@.m_@prop@.vptr()[id.index()];";
		};
	}
	return o;
}
basic_builder& make_single_setter(basic_builder& o, std::string const& type, bool by_reference) {
	o + substitute{ "type", type };
	if(by_reference) {
		o + "DCON_RELEASE_INLINE void @obj@_set_@prop@(@obj@_id id, @type@ const& value) noexcept" + block{
			o + "@obj@.m_@prop@.vptr()[id.index()] = value;";
		};
	} else {
		o + "DCON_RELEASE_INLINE void @obj@_set_@prop@(@obj@_id id, @type@ value) noexcept" + block{
			o + "@obj@.m_@prop@.vptr()[id.index()] = value;";
		};
	}
	return o;
}

basic_builder& make_bitfield_getters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	bool is_expandable) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name };
	o + heading{ "getters for @obj@: @prop@" };

	o + "DCON_RELEASE_INLINE bool @obj@_get_@prop@(@obj@_id id) const noexcept" + block{
		o + "return dcon::bit_vector_test(@obj@.m_@prop@.vptr()[id.index()]);";
	};
	make_vectorizable_getters(o, "ve::vbitfield_type", is_expandable);
	o + line_break{};
	return o;
}

basic_builder& make_bitfield_setters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	bool is_expandable) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name };
	o + heading{ "setters for @obj@: @prop@" };

	o + "DCON_RELEASE_INLINE void @obj@_set_@prop@(@obj@_id id, bool value) noexcept" + block{
		o + "dcon::bit_vector_set(@obj@.m_@prop@.vptr(), id.index(), value);";
	};

	make_vectorizable_setters(o, "ve::vbitfield_type", is_expandable);
	o + line_break{};
	return o;
}

basic_builder& make_object_getters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& type) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name };
	o + heading{ "getters for @obj@: @prop@" };

	make_single_getter(o, type, true);
	o + line_break{};
	return o;
}

basic_builder& make_object_setters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& type) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name };
	o + heading{ "setters for @obj@: @prop@" };

	make_single_setter(o, type, true);
	o + line_break{};
	return o;
}

basic_builder& make_vectorizable_type_getters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& type, bool is_expandable) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name };
	o + heading{ "getters for @obj@: @prop@" };

	make_single_getter(o, type, true);
	make_vectorizable_getters(o, std::string("ve::value_to_vector_type<") + type + ">", is_expandable);
	o + line_break{};
	return o;
}

basic_builder& make_vectorizable_type_setters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& type, bool is_expandable) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name };
	o + heading{ "setters for @obj@: @prop@" };

	make_single_setter(o, type, false);
	make_vectorizable_setters(o, std::string("ve::value_to_vector_type<") + type + ">", is_expandable);
	o + line_break{};
	return o;
}

basic_builder& make_special_array_getters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& type) {

	o + substitute{ "obj", object_name } +substitute{ "prop", property_name }+substitute{ "type", type };;
	o + heading{ "getters for @obj@: @prop@ (special array)" };

	o + "std::pair<@type@*, @type@*> @obj@_get_@prop@_range(@obj@_id id) const noexcept" + block{
		o + "return dcon::get_range(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()]);";
	};

	o + "@type@& @obj@_get_@prop@_at(@obj@_id id, uint32_t inner_index) const noexcept" + block{
		o + "return dcon::get(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()], inner_index);";
	};

	o + "uint32_t @obj@_get_@prop@_capacity(@obj@_id id) const noexcept" + block{
		o + "return dcon::get_capacity(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()]);";
	};

	o + "uint32_t @obj@_get_@prop@_size(@obj@_id id) const noexcept" + block{
		o + "return dcon::get_size(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()]);";
	};

	o + "bool @obj@_@prop@_contains(@obj@_id id, @type@ obj) const noexcept" + block{
		o + "return dcon::contains_item(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()], obj);";
	};
	o + line_break{};
	return o;

}
basic_builder& make_special_array_setters(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& type) {
	o + substitute{ "obj", object_name } +substitute{ "prop", property_name }+substitute{ "type", type };;
	o + heading{ "setters for @obj@: @prop@ (special array)" };

	o + "void @obj@_@prop@_push_back(@obj@_id id, @type@ obj) noexcept" + block{
		o + "return dcon::push_back(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()], obj);";
	};

	o + "void @obj@_@prop@_pop_back(@obj@_id id) noexcept" + block{
		o + "return dcon::pop_back(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()]);";
	};

	o + "void @obj@_@prop@_add_unique(@obj@_id id, @prop@ obj) noexcept" + block{
		o + "return dcon::add_unique_item(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()], obj);";
	};

	o + "void @obj@_@prop@_remove_unique(@obj@_id id, @prop@ obj) noexcept" + block{
		o + "return dcon::remove_unique_item(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[id.index()], obj);";
	};
	o + line_break{};
	return o;
}

basic_builder& make_relation_pk_getters_setters(basic_builder& o, std::string const& relation_name, std::string const& property_name,
	bool is_expandable) {
	o + substitute{ "rel", relation_name } +substitute{ "prop", property_name } +
		substitute{ "vector_position", is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" };
	o + heading{ "primary key getters and setters for @rel@: @prop@" };

	o + "DCON_RELEASE_INLINE @rel@_id @rel@_get_@prop@(@rel@_id id) const noexcept" + block{
		o + "return id;";
	};

	o + "DCON_RELEASE_INLINE ve::tagged_vector<@rel@_id> @rel@_get_@prop@(@vector_position@<@rel@_id> id) const noexcept" + block{
		o + "return ve::apply([](@rel@_id i){ return i; }, id);";
	};

	o + "DCON_RELEASE_INLINE ve::tagged_vector<@rel@_id> @rel@_get_@prop@(ve::partial_contiguous_tags<@rel@_id> id) const noexcept" + block{
		o + "return ve::apply([](@rel@_id i){ return i; }, id);";
	};

	o + "DCON_RELEASE_INLINE ve::tagged_vector<@rel@_id> @rel@_get_@prop@(ve::tagged_vector<@rel@_id> id) const noexcept" + block{
		o + "return id;";
	};

	o + "void @rel@_set_@prop@(@rel@_id id, @rel@_id value) noexcept" + block{
		o + "if(bool(value))" + block{
			o + "delete_@rel@(value);"
			+ "internal_move_relationship_@rel@(id, value);";
		} + append{"else"} + block{
			o + "delete_@rel@(id);";
		};
	};

	o + "bool @rel@_try_set_@prop@(@rel@_id id, @rel@_id value) noexcept" + block{
		o + "if(bool(value))" + block{
			o + "if(is_valid_@rel@(value)) return false;";
			o + "internal_move_relationship_@rel@(id, value);";
		} +append{"else"} +block{
			o + "delete_@rel@(id);";
		};
		o + "return true;";
	};

	o + line_break{};
	return o;
}

basic_builder& make_relation_pk_reverse_getters_setters(basic_builder& o, std::string const& relation_name,
	std::string const& property_name, std::string const& property_type, bool is_expandable, bool skip_as) {
	o + substitute{ "rel", relation_name } + substitute{ "prop", property_name } + substitute{ "prop_type", property_type }
		+ substitute{ "vector_position", is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" }
		+ substitute{ "as_suffix", skip_as ? std::string("") : std::string("_as_") + property_name };

	o + "DCON_RELEASE_INLINE @rel@_id get_@rel@_from_@prop_type@@as_suffix@(@prop_type@_id id) const noexcept" + block{
		o + "return id;";
	};

	o + "DCON_RELEASE_INLINE ve::tagged_vector<@rel@_id> get_@rel@_from_@prop_type@@as_suffix@"
		"(@vector_position@<@prop_type@_id> id) const noexcept" + block{
		o + "return ve::apply([](@rel@_id i){ return i; }, id);";
	};
	o + "DCON_RELEASE_INLINE ve::tagged_vector<@rel@_id> get_@rel@_from_@prop_type@@as_suffix@"
		"(ve::partial_contiguous_tags<@prop_type@_id> id) const noexcept" + block{
		o + "return ve::apply([](@rel@_id i){ return i; }, id);";
	};

	o + "DCON_RELEASE_INLINE ve::tagged_vector<@rel@_id> get_@rel@_from_@prop_type@@as_suffix@"
		"(ve::tagged_vector<@prop_type@_id> id) const noexcept" + block{
		o + "return id;";
	};

	o + "DCON_RELEASE_INLINE void @prop_type@_remove_@rel@@as_suffix@(@prop_type@_id id) noexcept" + block{
		o + "if(is_valid_@rel@(id))" + block{
			o + "delete_@rel@(id);";
		};
	};

	o + line_break{};
	return o;
}

basic_builder& make_relation_unique_non_pk_getters_setters(basic_builder& o, std::string const& relation_name,
	std::string const& property_name, std::string const& property_type, bool is_expandable) {

	o + substitute{ "obj", relation_name } +substitute{ "prop", property_name } +substitute{ "type", property_type + "_id"};
	o + heading{ "unique key getters and setters for @obj@: @prop@" };

	make_single_getter(o, property_type + "_id", false);
	make_vectorizable_getters(o, std::string("ve::value_to_vector_type<") + property_type + "_id>", is_expandable);

	o + substitute{ "type", property_type + "_id" };

	o + "void @obj@_set_@prop@(@obj@_id id, @type@ value) noexcept" + block{
		o + "if(auto old_value = @obj@.m_@prop@.vptr()[id.index()]; bool(old_value))" + block{
			o + "@obj@.m_link_back_@prop@.vptr()[old_value].index()] = @rel@_id();";
		};
		o + "if(bool(value))" + block{
			o + "if(auto old_rel = @rel@.m_link_back_@prop@.vptr()[value.index()]; bool(old_rel))" + block{
				o + "delete_@obj@(old_rel);";
			};
			o + "@obj@.m_link_back_@prop@.vptr()[value.index()] = id;";
		};
	};

	o + "bool @obj@_try_set_@prop@(@obj@_id id, @type@ value) noexcept" + block{
		o + "if(bool(value))" + block{
			o + "if(auto old_rel = @rel@.m_link_back_@prop@.vptr()[value.index()]; bool(old_rel))" + block{
				o + "return false;";
			};
			o + "@obj@.m_link_back_@prop@.vptr()[value.index()] = id;";
		};
		o + "if(auto old_value = @obj@.m_@prop@.vptr()[id.index()]; bool(old_value))" + block{
				o + "@obj@.m_link_back_@prop@.vptr()[old_value].index()] = @rel@_id();";
		};
		o + "return true;";
	};

	o + line_break{};
	return o;
}

basic_builder& make_relation_unique_non_pk_reverse_getters_setters(basic_builder& o, std::string const& relation_name,
	std::string const& property_name, std::string const& property_type, bool is_expandable, bool skip_as) {

	o + substitute{ "obj", relation_name } +substitute{ "prop", property_name } +substitute{ "type", property_type }
		+substitute{ "vector_position", is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" }
		+substitute{ "as_suffix", skip_as ? std::string("") : std::string("_as_") + property_name };

	o + "DCON_RELEASE_INLINE @obj@_id get_@obj@_from_@type@@as_suffix@(@type@_id id) const noexcept" + block{
		o + "return @obj@.m_link_back_@prop@.vptr()[id.index()];";
	};
	o + "DCON_RELEASE_INLINE ve::tagged_vector<@obj@_id> get_@obj@_from_@type@@as_suffix@"
		"(@vector_position@<@type@_id> id) const noexcept" + block{
		o + "return ve::load(id, @obj@.m_link_back_@prop@.vptr());";
	};
	o + "DCON_RELEASE_INLINE ve::tagged_vector<@obj@_id> get_@obj@_from_@type@@as_suffix@"
		"(partial_contiguous_tags<@type@_id> id) const noexcept" + block{
		o + "return ve::load(id, @obj@.m_link_back_@prop@.vptr());";
	};
	o + "DCON_RELEASE_INLINE ve::tagged_vector<@obj@_id> get_@obj@_from_@type@@as_suffix@"
		"(tagged_vector<@type@_id> id) const noexcept" + block{
		o + "return ve::load(id, @obj@.m_link_back_@prop@.vptr());";
	};
	o + "DCON_RELEASE_INLINE void @type@_remove_@obj@@as_suffix@(@type@_id id) noexcept" + block{
		o + "if(auto backid = @obj@.m_link_back_@prop@.vptr()[value.index()]; bool(backid))" + block{
			o + "delete_@obj@(backid);";
		};
	};

	o + line_break{};
	return o;
}

basic_builder& make_relation_many_getters_setters(basic_builder& o, std::string const& relation_name, list_type ltype,
	std::string const& property_name, std::string const& property_type, bool is_expandable) {

	o + substitute{ "obj", relation_name } +substitute{ "prop", property_name } +substitute{ "type", property_type + "_id" };
	o + heading{ "many key getters and setters for @obj@: @prop@" };

	make_single_getter(o, property_type + "_id", false);
	make_vectorizable_getters(o, std::string("ve::value_to_vector_type<") + property_type + "_id>", is_expandable);

	o + substitute{ "type", property_type + "_id" };

	o + "void @obj@_set_@prop@(@obj@_id id, @type@ value) noexcept" + block{
		if(ltype == list_type::list) {
			o + "if(auto old_value = @obj@.m_@prop@.vptr()[id.index()]; bool(old_value))" + block{
				o + "if(auto old_left = @obj@.m_link_@prop@.vptr()[id.index()].left; bool(old_left))" + block{
					o + "@obj@.m_link_@prop@.vptr()[old_left.index()].right = @obj@.m_link_@prop@.vptr()[id.index()].right;";
				} +append{"else"} +block{ // else: was the current head of the existing list
					o + "@obj@.m_head_back_@prop@.vptr()[old_value.index()] = m_link_@prop@.vptr()[id.index()].right;";
				};
				o + "if(auto old_right = @obj@.m_link_@prop@.vptr()[id.index()].right; bool(old_right))" + block{
					o + "@obj@.m_link_@prop@.vptr()[old_right.index()].left = @obj@.m_link_@prop@.vptr()[id.index()].left;";
				};
			};
			o + "if(bool(value))" + block{
				o + "if(auto existing_list = @obj@.m_head_back_@prop@.vptr()[value.index()]]; bool(existing_list))" + block{
					o + "@obj@.m_link_@prop@.vptr()[id.index()].left = existing_list;";
					o + "if(auto r = @obj@.m_link_@prop@.vptr()[existing_list.index()].right; bool(r))" + block{
						o + "@obj@.m_link_@prop@.vptr()[id.index()].right = r;";
						o + "@obj@.m_link_@prop@.vptr()[r.index()].left = id;";
					} +append{ "else" } +block{
						o + "@obj@.m_link_@prop@.vptr()[id.index()].right = @obj@_id()";
					};
					o + "@obj@.m_link_@prop@.vptr()[existing_list.index()].right = id;";
					o + "@obj@.m_head_back_@prop@.vptr()[value.index()]] = existing_list;";
				} +append{"else"} +block{
					o + "@obj@.m_head_back_@prop@.vptr()[value.index()]] = id;";
					o + "@obj@.m_link_@prop@.vptr()[id.index()].right = @obj@_id();";
					o + "@obj@.m_link_@prop@.vptr()[id.index()].left = @obj@_id();";
				};
			} +append{ "else" } +block{
				o + "@obj@.m_link_@prop@.vptr()[id.index()].right = @obj@_id();";
				o + "@obj@.m_link_@prop@.vptr()[id.index()].left = @obj@_id();";
			};
			o + "@obj@.m_@prop@.vptr()[id.index()] = value;";
		} else if(ltype == list_type::array) {
			o + "if(auto old_value = @obj@.m_@prop@.vptr()[id.index()]; bool(old_value))" + block{
				o + "auto& vref = @obj@.m_array_@prop@.vptr()[old_value.index()];";
				o + "dcon::remove_unique_item(@obj@.@prop@_storage, vref, id)";
			};
			o + "if(bool(value))" + block{
				o + "dcon::push_back(@obj@.@prop@_storage, @obj@.m_array_@prop@.vptr()[value.index()], id);";
			};
			o + "@obj@.m_@prop@.vptr()[id.index()] = value;";
		} else if(ltype == list_type::std_vector) {
			o + "if(auto old_value = @obj@.m_@prop@.vptr()[id.index()]; bool(old_value))" + block{
				o + "auto& vref = @obj@.m_array_@prop@.vptr()[old_value.index()];";
				o + "if(auto it = std::find(vref.begin(), vref.end(), id); it != vref.end())" + block{
					o + "*it = vref.back();";
					o + "vref.pop_back();";
				};
			};
			o + "if(bool(value))" + block{
				o + "@obj@.m_array_@prop@.vptr()[value.index()].push_back(id);";
			};
			o + "@obj@.m_@prop@.vptr()[id.index()] = value;";
		}
	};


	o + line_break{};
	return o;
}

basic_builder& make_relation_many_reverse_getters_setters(basic_builder& o, std::string const& relation_name, list_type ltype,
	std::string const& property_name, std::string const& property_type, bool is_expandable, bool skip_as) {

	o + substitute{ "obj", relation_name } +substitute{ "prop", property_name } +substitute{ "type", property_type }
		+substitute{ "vector_position", is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" }
		+substitute{ "as_suffix", skip_as ? std::string("") : std::string("_as_") + property_name };

	o + "template<typename T>";
	o + "DCON_RELEASE_INLINE void @type@_for_each_@obj@@as_suffix@(@type@_id id, T&& func) const" + block{
		o + "if(bool(id))" + block{
			if(ltype == list_type::list) {
				o + "for(auto list_pos = @obj@.m_head_back_@prop@.vptr()[id.index()]; "
					"bool(list_pos); list_pos = @obj@.m_link_@prop@.vptr()[list_pos.index()].right)" + block{
					o + "func(list_pos);";
				};
			} else if(ltype == list_type::array) {
				o + "auto vrange = dcon::get_range(@obj@.@prop@_storage, @obj@.m_array_@prop@.vptr()[id.index()]);";
				o + "std::for_each(vrange.first, vrange.second, func);";
			} else if(ltype == list_type::std_vector) {
				o + "auto& vref = @obj@.m_array_@prop@.vptr()[id.index()];";
				o + "std::for_each(vref.begin(), vref.end(), func);";
			}
		};
	};

	if(ltype == list_type::array) {
		o + "DCON_RELEASE_INLINE std::pair<@obj@_id const*, @obj@_id const*> @type@_range_of_@obj@@as_suffix@"
			"(@type@_id id) const" + block{
			o + "if(bool(id))" + block{
				o + "auto vrange = dcon::get_range(@obj@.@prop@_storage, @obj@.m_array_@prop@.vptr()[id.index()]);";
				o + "return std::pair<@obj@_id const*, @obj@_id const*>(vrange.first, vrange.second);";
			} +append{"else"} +block{
				o + "return std::pair<@obj@_id const*, @obj@_id const*>(nullptr, nullptr);";
			};
		};
	} else if(ltype == list_type::std_vector) {
		o + "DCON_RELEASE_INLINE std::pair<@obj@_id const*, @obj@_id const*> @type@_range_of_@obj@@as_suffix@"
			"(@type@_id id) const" + block{
			o + "if(bool(id))" + block{
				o + "auto& vref = @obj@.m_array_@prop@.vptr()[id.index()];";
				o + "return std::pair<@obj@_id const*, @obj@_id const*>(&(*vref.begin()), &(*vref.end()));";
			} +append{"else"} +block{
				o + "return std::pair<@obj@_id const*, @obj@_id const*>(nullptr, nullptr);";
			};
		};
	}

	o + "void @type@_remove_all_@obj@@as_suffix@(@type@_id id) noexcept" + block{
		if(ltype == list_type::array || ltype == list_type::std_vector) {
			o + "auto rng = @type@_range_of_@obj@_as_@prop@(id);";
			o + "std::vector<@obj@_id> temp(rng.first, rng.second);";
			o + "std::for_each(temp.begin(), temp.end(), [t = this](@obj@_id i) { t->delete_@obj@(i); });";
		} else {
			o + "std::vector<@obj@_id> temp;";
			o + "@type@_for_each_@obj@_as_@prop@(id, [&](@obj@_id j) { temp.push_back(j); });";
			o + "std::for_each(temp.begin(), temp.end(), [t = this](@obj@_id i) { t->delete_@obj@(i); });";
		}
	};


	o + line_break{};
	return o;
}

basic_builder& clear_value(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& property_type_val, property_type type, std::string const& at) {

	o + substitute{ "t_obj", object_name } + substitute{ "t_prop", property_name } + substitute{ "t_at", at }
		+substitute{ "t_type", property_type_val };

	if(type == property_type::special_vector) {
		o + "@t_obj@.@t_prop@_storage.release(@t_obj@.m_@t_prop@.vptr()[@t_at@.index()]);";
	} else if(type == property_type::bitfield) {
		o + "dcon::bit_vector_set(@t_obj@.m_@t_prop@.vptr(), @t_at@.index(), false);";
	} else {
		o + "@t_obj@.m_@t_prop@.vptr()[@t_at@.index()] = @t_type@{};";
	}

	return o;
}

basic_builder& pop_value(basic_builder& o, std::string const& object_name, std::string const& property_name,
	property_type type, std::string const& at, bool array_name = false) {

	o + substitute{ "t_obj", object_name } +substitute{ "t_prop", property_name } +substitute{ "t_at", at };

	if(type == property_type::special_vector) {
		if(array_name) {
			o + "@t_obj@.@t_prop@_storage.release(@t_obj@.m_array_@t_prop@.vptr()[@t_at@.index()]);";
			o + "@t_obj@.m_array_@t_prop@.values.pop_back();";
		} else {
			o + "@t_obj@.@t_prop@_storage.release(@t_obj@.m_@t_prop@.vptr()[@t_at@.index()]);";
			o + "@t_obj@.m_@t_prop@.values.pop_back();";
		}
	} else if(type == property_type::bitfield) {
		o + "dcon::bit_vector_set(@t_obj@.m_@t_prop@.vptr(), @t_at@.index(), false);";
		o + "@t_obj@.m_@t_prop@.values.resize(1 + (@t_obj@.size_used + 6) / 8);";
	} else {
		o + "@t_obj@.m_@t_prop@.values.pop_back();";
	}

	return o;
}

basic_builder& make_pop_back(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name };

	o + heading{ "container pop_back for @obj@" };

	o + "void @obj@_pop_back()" + block{
		o + "if(@obj@.size_used == 0) return;";
		o + "@obj@_id id_removed(@obj@_id::value_base_t(@obj@.size_used - 1));";

		if(cob.hook_delete)
			o + "on_delete_@obj@(id_removed);";

		for(auto& iob : cob.indexed_objects) {
			o + substitute{ "i_prop_name", iob.property_name } +substitute{ "i_type_name", iob.type_name };
			o + "@obj@_set_@i_prop_name@(id_removed, @i_type_name@_id());";
			if(cob.is_expandable) {
				o + "@obj@.m_@i_prop_name@.values.pop_back();";
				if(iob.ltype == list_type::list)
					o + "@obj@.m_link_@i_prop_name@.values.pop_back();";
			}
		}

		for(auto& cr : cob.relationships_involved_in) {
			o + substitute{ "rel_name", cr.relation_name } +substitute{"rel_prop_name", cr.property_name };
			if(cr.as_primary_key) {
				if(cob.is_expandable) {
					o + "@rel_name@_pop_back();";
				} else {
					o + "delete_@rel_name@(id_removed);";
					o + "@rel_name@.size_used = @obj@.size_used - 1;";
				}
				if(cob.is_expandable) {
					for(auto& rp : cr.rel_ptr->properties) {
						if(!rp.is_derived) {
							pop_value(o, cr.relation_name, rp.name, rp.type, "id_removed");
						}
					}
					for(auto& ri : cr.rel_ptr->indexed_objects) {
						if(cr.rel_ptr->primary_key == ri) {
						} else {
							pop_value(o, cr.relation_name, ri.property_name, property_type::other, "id_removed");
						}
						if(ri.ltype == list_type::list && ri.index == index_type::many) {
							pop_value(o, cr.relation_name, std::string("link_") + ri.property_name,
								property_type::other, "id_removed");
						}
					}
				} else {
					for(auto& rp : cr.rel_ptr->properties) {
						if(!rp.is_derived) {
							clear_value(o, cr.relation_name, rp.name, rp.data_type, rp.type, "id_removed");
						}
					}
				}
			} else if(cr.indexed_as == index_type::at_most_one) {
				o + "@obj@_remove_@rel_name@_as_@rel_prop_name@(id_removed));";
				if(cob.is_expandable) {
					pop_value(o, cr.relation_name, std::string("link_back_") + cr.property_name,
						property_type::other, "id_removed");
				}
			} else if(cr.indexed_as == index_type::many) {
				o + "@obj@_remove_all_@rel_name@_as_@rel_prop_name@(id_removed);";
				if(cob.is_expandable) {
					if(cr.listed_as == list_type::list) {
						pop_value(o, cr.relation_name, std::string("head_back_") + cr.property_name,
							property_type::other, "id_removed");
					} else if(cr.listed_as == list_type::array) {
						pop_value(o, cr.relation_name, cr.property_name,
							property_type::special_vector, "id_removed", true);
					} else if(cr.listed_as == list_type::std_vector) {
						pop_value(o, cr.relation_name, std::string("array_") + cr.property_name,
							property_type::other, "id_removed");
					}
				}
			}
		}
		if(cob.is_expandable) {
			for(auto& cp : cob.properties) {
				if(!cp.is_derived) {
					pop_value(o, cob.name, cp.name, cp.type, "id_removed");
				}
			}
		} else {
			for(auto& cp : cob.properties) {
				if(!cp.is_derived) {
					clear_value(o, cob.name, cp.name, cp.data_type, cp.type, "id_removed");
				}
			}
		}
		o + "--@obj@.size_used;";
	};


	o + line_break{};
	return o;
}

basic_builder& clear_value_range(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& property_type_val, property_type type, std::string const& start, std::string const& count,
	bool array_name = false) {

	o + substitute{ "t_obj", object_name } +substitute{ "t_prop", property_name } +substitute{ "begin", start }
	+substitute{ "t_type", property_type_val } +substitute{ "count", count };

	if(type == property_type::special_vector) {
		if(array_name) {
			o + "std::for_each(@t_obj@.m_array_@t_prop@.vptr() + @begin@, @t_obj@.m_array_@t_prop@.vptr() + @begin@ + @count@, "
				"[t = this](dcon::stable_mk_2_tag& i){ t->@t_obj@.@t_prop@_storage.release(i); });";
		} else {
			o + "std::for_each(@t_obj@.m_@t_prop@.vptr() + @begin@, @t_obj@.m_@t_prop@.vptr() + @begin@ + @count@, "
				"[t = this](dcon::stable_mk_2_tag& i){ t->@t_obj@.@t_prop@_storage.release(i); });";
		}
	} else if(type == property_type::bitfield) {
		o + "for(uint32_t i = @begin@; i < 8 * (((@begin@ + 7) / 8)); ++i)" + block{
			o + "dcon::bit_vector_set(@t_obj@.m_@t_prop@.vptr(), i, false);";
		};
		o + "std::fill_n(@t_obj@.m_@t_prop@.vptr() + (@begin@ + 7) / 8, (@begin@ + @count@ + 7) / 8 - (@begin@ + 7) / 8, dcon::bitfield_type{0});";
	} else if(type == property_type::object) {
		o + "std::destroy_n(@t_obj@.m_@t_prop@.vptr() + @begin@, @count@);";
		o + "std::uninitialized_default_construct_n(@t_obj@.m_@t_prop@.vptr() + @begin@, @count@);";
	} else {
		o + "std::fill_n(@t_obj@.m_@t_prop@.vptr() + @begin@, @count@, @t_type@{});";
	}
	return o;
}
basic_builder& shrink_value_range(basic_builder& o, std::string const& object_name, std::string const& property_name,
	property_type type, std::string const& start, std::string const& count, bool array_name = false) {

	o + substitute{ "t_obj", object_name } +substitute{ "t_prop", property_name } +substitute{ "begin", start }
	+substitute{ "count", count };

	if(type == property_type::special_vector) {
		if(array_name) {
			o + "std::for_each(@t_obj@.m_array_@t_prop@.vptr() + @begin@, @t_obj@.m_array_@t_prop@.vptr() + @begin@ + @count@, "
				"[t = this](dcon::stable_mk_2_tag& i){ t->@t_obj@.@t_prop@_storage.release(i); });";
			o + "@t_obj@.m_array_@t_prop@.values.resize(1 + @begin@);";
		} else {
			o + "std::for_each(@t_obj@.m_@t_prop@.vptr() + @begin@, @t_obj@.m_@t_prop@.vptr() + @begin@ + @count@, "
				"[t = this](dcon::stable_mk_2_tag& i){ t->@t_obj@.@t_prop@_storage.release(i); });";
			o + "@t_obj@.m_@t_prop@.values.resize(1 + @begin@);";
		}
	} else if(type == property_type::bitfield) {
		o + "for(uint32_t i = @begin@; i < 8 * (((@begin@ + 7) / 8)); ++i)" + block{
			o + "dcon::bit_vector_set(@t_obj@.m_@t_prop@.vptr(), i, false);";
		};
		o + "@t_obj@.m_@t_prop@.values.resize(1 + (@begin@ + 7) / 8);";
	} else {
		o + "@t_obj@.m_@t_prop@.values.resize(1 + @begin@);";
	}
	return o;
}

basic_builder& grow_value_range(basic_builder& o, std::string const& object_name, std::string const& property_name,
	property_type type, std::string const& size, bool array_name = false) {

	o + substitute{ "t_obj", object_name } +substitute{ "t_prop", property_name } +substitute{ "size", size };

	if(type == property_type::special_vector) {
		if(array_name) {
			o + "@t_obj@.m_array_@t_prop@.values.resize(1 + @size@, std::numeric_limits<dcon::stable_mk_2_tag>::max());";
		} else {
			o + "@t_obj@.m_@t_prop@.values.resize(1 + @size@, std::numeric_limits<dcon::stable_mk_2_tag>::max());";
		}
	} else if(type == property_type::bitfield) {
		o + "@t_obj@.m_@t_prop@.values.resize(1 + (@size@ + 7) / 8);";
	} else {
		o + "@t_obj@.m_@t_prop@.values.resize(1 + @size@);";
	}
	return o;
}

basic_builder& make_object_resize(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name } +substitute{ "obj_sz", std::to_string(cob.size) };
	o + heading{ "container resize for @obj@" };

	o + "void @obj@_resize(uint32_t new_size)" + block{
		if(!cob.is_expandable) {
			o + "if(new_size > @obj_sz@) std::abort();";
		}
		o + "const uint32_t old_size = @obj@.size_used;";
		o + "if(new_size < old_size)" + block{ // contracting
			if(cob.store_type == storage_type::erasable) {
				o + "@obj@.first_free = @obj@_id();";
				if(cob.is_expandable) {
					o + "@obj@.m__index.values.resize(new_size + 1);";
					o + "int32_t i = int32_t(new_size);";
				} else {
					o + "int32_t i = int32_t(@obj_sz@);";
					o + "for(; i >= new_size; --i)" + block{
						o + "@obj@.m__index.vptr()[i] = @obj@.first_free;";
						o + "@obj@.first_free = @obj@_id(@obj@_id::value_base_t(i));";
					};
				}
				o + "for(; i >= 0; --i)" + block{
					o + "if(@obj@.m__index.vptr()[i] != @obj@_id(@obj@_id::value_base_t(i)))" + block{
						o + "@obj@.m__index.vptr()[i] = @obj@.first_free;";
						o + "@obj@.first_free = @obj@_id(@obj@_id::value_base_t(i));";
					};
				};
			}
			for(auto& io : cob.indexed_objects) {
				if(cob.primary_key != io) {
					if(!cob.is_expandable) {
						clear_value_range(o, cob.name, io.property_name, io.type_name + "_id", property_type::other,
							"0", "old_size");
					} else {
						shrink_value_range(o, cob.name, io.property_name, property_type::other, "new_size", "old_size - new_size");
						clear_value_range(o, cob.name, io.property_name, io.type_name + "_id", property_type::other,
							"0", "new_size");
					}

					if(io.index == index_type::at_most_one) {
						clear_value_range(o, cob.name, std::string("link_back_") + io.property_name, cob.name + "_id",
							property_type::other, "0", io.type_name + ".size_used");
					} else if(io.index == index_type::many) {
						if(io.ltype == list_type::list) {
							if(!cob.is_expandable) {
								clear_value_range(o, cob.name, std::string("link_") + io.property_name,
									cob.name + "_id_pair", property_type::other, "0", "old_size");
							} else {
								shrink_value_range(o, cob.name, std::string("link_") + io.property_name,
									property_type::other, "new_size", "old_size - new_size");
								clear_value_range(o, cob.name, std::string("link_") + io.property_name,
									cob.name + "_id_pair", property_type::other, "0", "new_size");
							}
							clear_value_range(o, cob.name, std::string("head_back_") + io.property_name, cob.name + "_id",
								property_type::other, "0", io.type_name + ".size_used");
						} else if(io.ltype == list_type::array) {
							clear_value_range(o, cob.name, io.property_name, "",
								property_type::special_vector, "0", io.type_name + ".size_used", true);
						} else if(io.ltype == list_type::std_vector) {
							clear_value_range(o, cob.name, std::string("array_") + io.property_name, std::string("std::vector<") + cob.name + "_id>",
								property_type::other, "0", io.type_name + ".size_used");
						}
					}
				}
			}
			for(auto& cp : cob.properties) {
				if(!cp.is_derived) {
					if(!cob.is_expandable)
						clear_value_range(o, cob.name, cp.name, cp.data_type, cp.type, "new_size", "old_size - new_size");
					else
						shrink_value_range(o, cob.name, cp.name, cp.type, "new_size", "old_size - new_size");
				}
			}
			for(auto& cr : cob.relationships_involved_in) {
				if(cr.as_primary_key) {
					o + (cr.relation_name + "_resize(new_size);");
				} else if(cr.indexed_as == index_type::at_most_one) {
					if(!cob.is_expandable) {
						clear_value_range(o, cr.relation_name, std::string("link_back_") + cr.property_name,
							cr.relation_name + "_id", property_type::other, "0", "old_size");
					} else {
						shrink_value_range(o, cr.relation_name, std::string("link_back_") + cr.property_name,
							property_type::other, "new_size", "old_size - new_size");
						clear_value_range(o, cr.relation_name, std::string("link_back_") + cr.property_name,
							cr.relation_name + "_id", property_type::other, "0", "new_size");
					}
					clear_value_range(o, cr.relation_name, cr.property_name, cob.name + "_id", property_type::other,
						"0", cr.relation_name + ".size_used");
				} else if(cr.indexed_as == index_type::many) {
					if(cr.listed_as == list_type::list) {
						if(!cob.is_expandable) {
							clear_value_range(o, cr.relation_name, std::string("head_back_") + cr.property_name,
								cr.relation_name + "_id", property_type::other, "0", "old_size");
						} else {
							shrink_value_range(o, cr.relation_name, std::string("head_back_") + cr.property_name,
								property_type::other, "new_size", "old_size - new_size");
							clear_value_range(o, cr.relation_name, std::string("head_back_") + cr.property_name,
								cr.relation_name + "_id", property_type::other, "0", "new_size");
						}
						clear_value_range(o, cr.relation_name, std::string("link_") + cr.property_name, cr.relation_name + "_id_pair",
							property_type::other, "0", cr.relation_name + ".size_used");
					} else if(cr.listed_as == list_type::array) {
						if(!cob.is_expandable) {
							clear_value_range(o, cr.relation_name, cr.property_name,
								"", property_type::special_vector, "0", "old_size", true);
						} else {
							shrink_value_range(o, cr.relation_name, cr.property_name,
								property_type::special_vector, "new_size", "old_size - new_size", true);
							clear_value_range(o, cr.relation_name, cr.property_name,
								"", property_type::special_vector, "0", "new_size", true);
						}
					} else if(cr.listed_as == list_type::std_vector) {
						if(!cob.is_expandable) {
							clear_value_range(o, cr.relation_name, std::string("array_") + cr.property_name,
								std::string("std::vector<") + cr.relation_name + "_id>", property_type::object,
								"0", "old_size");
						} else {
							shrink_value_range(o, cr.relation_name, std::string("array_") + cr.property_name,
								property_type::object, "new_size", "old_size - new_size");
							clear_value_range(o, cr.relation_name, std::string("array_") + cr.property_name,
								std::string("std::vector<") + cr.relation_name + "_id>", property_type::object,
								"0", "new_size");
						}
					}
					clear_value_range(o, cr.relation_name, cr.property_name, cob.name + "_id", property_type::other,
						"0", cr.relation_name + ".size_used");
				}
			}
		} + append{ "else if(new_size > old_size)" } + block{ // expanding
			if(cob.store_type == storage_type::erasable) {
				o + "@obj@.first_free = @obj@_id();";
				if(cob.is_expandable) {
					o + "@obj@.m__index.values.resize(new_size + 1);";
					o + "int32_t i = int32_t(new_size);";
				} else {
					o + "int32_t i = int32_t(@obj_sz@);";
					o + "for(; i >= old_size; --i)" + block{
						o + "@obj@.m__index.vptr()[i] = @obj@.first_free;";
						o + "@obj@.first_free = @obj@_id(@obj@_id::value_base_t(i));";
					};
				}
				o + "for(; i >= 0; --i)" + block{
					o + "if(@obj@.m__index.vptr()[i] != @obj@_id(@obj@_id::value_base_t(i)))" + block{
						o + "@obj@.m__index.vptr()[i] = @obj@.first_free;";
						o + "@obj@.first_free = @obj@_id(@obj@_id::value_base_t(i));";
					};
				};
			}

			if(cob.is_expandable) {
				for(auto& cp : cob.properties) {
					if(!cp.is_derived) {
						grow_value_range(o, cob.name, cp.name, cp.type, "new_size");
					}
				}
				for(auto& io : cob.indexed_objects) {
					if(cob.primary_key != io) {
						grow_value_range(o, cob.name, io.property_name, property_type::other, "new_size");
					}
					if(io.index == index_type::many && io.ltype == list_type::list) {
						grow_value_range(o, cob.name, std::string("link_") + io.property_name, property_type::other, "new_size");
					}
				}

				for(auto& cr : cob.relationships_involved_in) {
					if(cr.as_primary_key) {
						o + (cr.relation_name + "_resize(new_size);");
					} else if(cr.indexed_as == index_type::at_most_one) {
						clear_value_range(o, cr.relation_name, std::string("link_back_") + cr.property_name,
							cr.relation_name + "_id", property_type::other, "0", "old_size");
						clear_value_range(o, cr.relation_name, cr.property_name, cob.name + "_id", property_type::other,
							"0", cr.relation_name + ".size_used");
						grow_value_range(o, cr.relation_name, std::string("link_back_") + cr.property_name,
							property_type::other, "new_size");
					} else if(cr.indexed_as == index_type::many) {
						if(cr.listed_as == list_type::list) {
							clear_value_range(o, cr.relation_name, std::string("head_back_") + cr.property_name,
								cr.relation_name + "_id", property_type::other, "0", "old_size");

							clear_value_range(o, cr.relation_name, std::string("link_") + cr.property_name, cr.relation_name + "_id_pair",
								property_type::other, "0", cr.relation_name + ".size_used");
						} else if(cr.listed_as == list_type::array) {
							clear_value_range(o, cr.relation_name, cr.property_name,
								"", property_type::special_vector, "0", "old_size", true);
						} else if(cr.listed_as == list_type::std_vector) {
							clear_value_range(o, cr.relation_name, std::string("array_") + cr.property_name,
								std::string("std::vector<") + cr.relation_name + "_id>", property_type::object,
								"0", "old_size");
						}

						clear_value_range(o, cr.relation_name, cr.property_name, cob.name + "_id", property_type::other,
							"0", cr.relation_name + ".size_used");

						if(cr.listed_as == list_type::list) {
							grow_value_range(o, cr.relation_name, std::string("head_back_") + cr.property_name,
								property_type::other, "new_size");
						} else if(cr.listed_as == list_type::array) {
							grow_value_range(o, cr.relation_name, cr.property_name,
								property_type::special_vector, "new_size", true);
						} else if(cr.listed_as == list_type::std_vector) {
							grow_value_range(o, cr.relation_name, std::string("array_") + cr.property_name,
								property_type::object, "new_size");
						}
					}
				}
			} else {
				for(auto& cr : cob.relationships_involved_in) {
					if(cr.as_primary_key) {
						o + (cr.relation_name + "_resize(new_size);");
					} else if(cr.indexed_as == index_type::at_most_one) {
						clear_value_range(o, cr.relation_name, std::string("link_back_") + cr.property_name,
							cr.relation_name + "_id", property_type::other, "0", "new_size");
						clear_value_range(o, cr.relation_name, cr.property_name, cob.name + "_id", property_type::other,
							"0", cr.relation_name + ".size_used");
					} else if(cr.indexed_as == index_type::many) {
						if(cr.listed_as == list_type::list) {
							clear_value_range(o, cr.relation_name, std::string("head_back_") + cr.property_name,
								cr.relation_name + "_id_pair", property_type::other, "0", "new_size");
						} else if(cr.listed_as == list_type::array) {
							clear_value_range(o, cr.relation_name, cr.property_name,
								"", property_type::special_vector, "0", "new_size", true);
						} else if(cr.listed_as == list_type::std_vector) {
							clear_value_range(o, cr.relation_name, std::string("array_") + cr.property_name,
								std::string("std::vector<") + cr.relation_name + "_id>", property_type::object,
								"0", "new_size");
						}
						clear_value_range(o, cr.relation_name, cr.property_name, cob.name + "_id", property_type::other,
							"0", cr.relation_name + ".size_used");
					}
				}
			}
			
		};

		o + "@obj@.size_used = new_size;";
	};
	o + line_break{};
	return o;
}

basic_builder& move_value_from_back(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& source, std::string const& destination, std::string const& null_value, bool is_expandable) {

	o + substitute{ "t_obj", object_name } +substitute{ "t_prop", property_name }
		+substitute{ "t_source", source } +substitute{ "t_dest", destination }
	+substitute{ "t_null", null_value };

	o + "@t_obj@.m_@t_prop@.vptr()[@t_dest@.index()] = std::move(@t_obj@.m_@t_prop@.vptr()[@t_source@.index()]);";
	if(is_expandable) {
		o + "@t_obj@.m_@t_prop@.values.pop_back();";
	} else {
		o + "@t_obj@.m_@t_prop@.vptr()[@t_source@.index()] = @t_null@;";
	}

	return o;
}

basic_builder& move_value(basic_builder& o, std::string const& object_name, std::string const& property_name,
	std::string const& source, std::string const& destination, std::string const& null_value) {

	o + substitute{ "t_obj", object_name } +substitute{ "t_prop", property_name }
		+substitute{ "t_source", source } +substitute{ "t_dest", destination }
	+substitute{ "t_null", null_value };

	o + "@t_obj@.m_@t_prop@.vptr()[@t_dest@.index()] = std::move(@t_obj@.m_@t_prop@.vptr()[@t_source@.index()]);";
	o + "@t_obj@.m_@t_prop@.vptr()[@t_source@.index()] = @t_null@;";
	
	return o;
}

basic_builder& make_compactable_delete(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name };
	o + heading{ "container compactable delete for @obj@" };

	o + "void delete_@obj@(@obj@_id id)" + block{ 
		o + "@obj@_id id_removed = id;";
		o + "@obj@_id last_id(@obj@_id::value_base_t(@obj@.size_used - 1));";
		o + "if(id_removed == last_id) { @obj@_pop_back(); return; }";
		if(cob.hook_delete)
			o + "on_delete_@obj@(id_removed);";

		for(auto& iob : cob.indexed_objects) {
			o + substitute{ "i_prop_name", iob.property_name } +substitute{ "i_type_name", iob.type_name };

			o + "@obj@_set_@i_prop_name@(id_removed, @i_type_name@_id());";

			if(iob.index == index_type::at_most_one) {
				o + "if(auto related = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(related))" + block{
					o + "@obj@.m_link_back_@i_prop@.vptr()[related.index()] = id_removed;";
				};
			} else if(iob.index == index_type::many) {
				if(iob.ltype == list_type::list) {
					o + "if(bool(@obj@.m_@i_prop@.vptr()[last_id.index()]))" + block{
						o + "auto tmp = @obj@.m_link_@i_prop@.vptr()[last_id.index()];";
						o + "if(bool(tmp.left))" + block{
							o + "@obj@.m_link_@i_prop@.vptr()[tmp.left.index()].right = id_removed;";
						} +append{ "else" } +block{
							o + "for(auto lpos = last_id; bool(lpos); lpos = @obj@.m_link_@i_prop@.vptr()[lpos.index()].right)" + block{
								o + "@obj@.m_head_back_@i_prop@.vptr()[@obj@.m_@i_prop@.vptr()[lpos.index()].index()] = id_removed;";
							};
						};
						o + "if(bool(tmp.right))" + block{
							o + "@obj@.m_link_@i_prop@.vptr()[tmp.right.index()].left = id_removed;";
						};
					};
					move_value_from_back(o, cob.name, std::string("link_") + iob.property_name,
						"last_id", "id_removed", cob.name + "_id_pair()", cob.is_expandable);
				} else if(iob.ltype == list_type::std_vector) {
					o + "if(auto tmp = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(tmp))" + block{
						o + "auto& vref = @obj@.m_array_@i_prop@.vptr()[tmp.index()];";
						o + "if(auto pos = std::find(vref.begin(), vref.end(), last_id); pos != range.second)" + block {
							o + "*pos = id_removed;";
						};
					};
				} else if(iob.ltype == list_type::array) {
					o + "if(auto tmp = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(tmp))" + block{
						o + "dcon::replace_unique_item(@obj@.@i_prop@_storage, @obj@.m_array_@i_prop@.vptr()[tmp.index()],"
							" last_id, id_removed);";
					};
				}
			}

			move_value_from_back(o, cob.name, iob.property_name,
				"last_id", "id_removed", iob.type_name + "_id()", cob.is_expandable);
		}
		for(auto& cr : cob.relationships_involved_in) {
			o + substitute{ "rel", cr.relation_name } +substitute{"r_prop", cr.property_name };
			if(cr.as_primary_key) {
				o + "delete_@rel@(id_removed);";
				o + "internal_move_relationship_@rel@(last_id, id_removed);";
				if(cob.is_expandable) {
					o + "@rel@_pop_back();";
				} else {
					o + "@rel@.size_used = @obj@.size_used - 1;";
				}
			} else if(cr.indexed_as == index_type::at_most_one) {
				o + "@obj@_remove_@rel@_as_@r_prop@(id_removed));";
				o + "if(auto bk = @rel@.m_link_back_@r_prop@.vptr()[last_id.index()]; bool(bk))" + block{
					o + "@rel@.m_@r_prop@.vptr()[bk.index()] = id_removed;"; 
				};
				move_value_from_back(o, cr.relation_name, std::string("link_back_") + cr.property_name,
					"last_id", "id_removed", cr.relation_name + "_id()", cob.is_expandable);
			} else if(cr.indexed_as == index_type::many) {
				o + "@obj@_remove_all_@rel@_as_@r_prop@(id_removed);";
				o + "obj_for_each_@rel@_as_@r_prop@(last_id, "
					"[t = this](@rel@_id i){ t->@rel@.m_@r_prop@.vptr()[i.index()] = id_removed; });";
				if(cr.listed_as == list_type::list) {
					move_value_from_back(o, cr.relation_name, std::string("head_back_") + cr.property_name,
						"last_id", "id_removed", cr.relation_name + "_id()", cob.is_expandable);
				} else if(cr.listed_as == list_type::array) {
					o + "@rel@.@r_prop@_storage.release(@rel@.m_array_@r_prop@.vptr()[id_removed.index()]);";
					move_value_from_back(o, cr.relation_name, std::string("array_") + cr.property_name,
						"last_id", "id_removed", "std::numeric_limits<dcon::stable_mk_2_tag>::max()", cob.is_expandable);
				} else if(cr.listed_as == list_type::std_vector) {
					move_value_from_back(o, cr.relation_name, std::string("array_") + cr.property_name,
						"last_id", "id_removed", "std::vector<@rel@_id>{}", cob.is_expandable);
				}
			}
		}
		for(auto& io : cob.indexed_objects) {
			if(cob.primary_key != io) {
				o + substitute{ "i_prop", io.property_name } +substitute{ "i_type", io.type_name };
				o + "@obj@_set_@i_prop@(id_removed, @i_type@_id());";

				if(io.index == index_type::at_most_one) {
					o + "if(auto related = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(related))" + block{
						o + "@obj@.m_link_back_@i_prop@.vptr()[related.index()] = id_removed;";
					};
				} else if(io.index == index_type::many) {
					if(io.ltype == list_type::list) {
						o + "if(bool(@obj@.m_@i_prop@.vptr()[last_id.index()]))" + block{
							o + "auto tmp = @obj@.m_link_@i_prop@.vptr()[last_id.index()];";
							o + "if(bool(tmp.left))" + block{
								o + "@obj@.m_link_@i_prop@.vptr()[tmp.left.index()].right = id_removed;";
							} +append{ "else" } +block{
								o + "for(auto lpos = last_id; bool(lpos); lpos = @obj@.m_link_@i_prop@.vptr()[lpos.index()].right)" + block{
									o + "@obj@.m_head_back_@i_prop@.vptr()[@obj@.m_@i_prop@.vptr()[lpos.index()].index()] = id_removed;";
								};
							};
							o + "if(bool(tmp.right))" + block{
								o + "@obj@.m_link_@i_prop@.vptr()[tmp.right.index()].left = id_removed;";
							};
						};
						move_value_from_back(o, cob.name, std::string("m_link_") + io.property_name,
							"last_id", "id_removed", cob.name + "_id_pair()", cob.is_expandable);
					} else if(io.ltype == list_type::std_vector) {
						o + "if(auto tmp = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(tmp))" + block{
							o + "auto& vref = @obj@.m_array_@i_prop@.vptr()[tmp.index()];";
							o + "if(auto pos = std::find(vref.begin(), vref.end(), last_id); pos != range.second)" + block {
								o + "*pos = id_removed;";
							};
						};
					} else if(io.ltype == list_type::array) {
						o + "if(auto tmp = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(tmp))" + block{
							o + "dcon::replace_unique_item(@obj@.@i_prop@_storage, @obj@.m_array_@i_prop@.vptr()[tmp.index()],"
								" last_id, id_removed);";
						};
					}
				}

				move_value_from_back(o, cob.name, io.property_name,
					"last_id", "id_removed", io.type_name + "_id()", cob.is_expandable);
			}
		}
		for(auto& cp : cob.properties) {
			o + substitute{ "prop", cp.name };
			if(cp.is_derived) {
			} else if(cp.type == property_type::special_vector) {
				o + "@obj@.@prop@_storage.release(@obj@.m_@prop@.vptr()[id_removed.index()]);";
				move_value_from_back(o, cob.name, cp.name,
					"last_id", "id_removed", "std::numeric_limits<dcon::stable_mk_2_tag>::max()", cob.is_expandable);
			} else if(cp.type == property_type::bitfield) {
				o + "dcon::bit_vector_set(@obj@.m_@prop@.vptr(), id_removed.index(), "
					"dcon::bit_vector_get(@obj@.m_@prop@.vptr(), last_id.index()));";
				o + "dcon::bit_vector_set(@obj@.m_@prop@.vptr(), last_id.index(), false);";
				if(cob.is_expandable)
					o + "@obj@.m_@prop@.values.resize(1 + (@obj@.size_used + 6) / 8);";
			} else {
				move_value_from_back(o, cob.name, cp.name,
					"last_id", "id_removed", cp.data_type + "{}", cob.is_expandable);
			}
		}
		o + "--@obj@.size_used;";
		if(cob.hook_move)
			o + "on_move_@obj@(id_removed, last_id);";
	};
	o + line_break{};
	return o;
}

basic_builder& expandable_push_back(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name };

	for(auto& cr : cob.relationships_involved_in) {
		o + substitute{ "rel", cr.relation_name } +substitute{ "in_rel_as", cr.property_name };
		if(cr.as_primary_key) {
			o + "@rel@.size_used = @obj@.size_used + 1;";
			for(auto& rp : cr.rel_ptr->properties) {
				o + substitute{ "r_prop", rp.name };
				if(rp.is_derived) {
				} if(rp.type == property_type::bitfield) {
					o + "@rel@.m_@r_prop@.values.resize(1 + (@obj@.size_used + 8) / 8);";
				} else if(rp.type == property_type::special_vector) {
					o + "@rel@.m_@r+prop@.values.push_back(std::numeric_limits<dcon::stable_mk_2_tag>::max());";
				} else {
					o + "@rel@.m_@r_prop@.values.emplace_back();";
				}
			}
			for(auto& ri : cr.rel_ptr->indexed_objects) {
				o + substitute{ "r_prop", ri.property_name };
				if(cr.rel_ptr->primary_key == ri) {
				} else {
					o + "@rel@.m_@r_prop@.values.emplace_back();";
				}
				if(ri.ltype == list_type::list && ri.index == index_type::many) {
					o + "@rel@.m_link_@r_propr@.values.emplace_back();";
				}
			}
		} else if(cr.indexed_as == index_type::at_most_one) {
			o + "@rel@.m_link_back_@in_rel_as@.values.emplace_back();";
		} else if(cr.indexed_as == index_type::many) {
			if(cr.listed_as == list_type::list) {
				o + "@rel@.m_head_back_@in_rel_as@.values.emplace_back();";
			} else if(cr.listed_as == list_type::array) {
				o + "@rel@.m_array_@in_rel_as@.values.push_back(std::numeric_limits<dcon::stable_mk_2_tag>::max());";
			} else if(cr.listed_as == list_type::std_vector) {
				o + "@rel@.m_array_@in_rel_as@.values.emplace_back();";
			}
		}
	}
	for(auto& io : cob.indexed_objects) {
		o + substitute{ "prop", io.property_name };
		if(io.index == index_type::at_most_one && cob.primary_key == io) {

		} else if(io.index == index_type::at_most_one) {
			o + "@obj@.m_@prop@.values.emplace_back();";
		} else if(io.index == index_type::many) {
			o + "@obj@.m_@prop@.values.emplace_back();";
			if(io.ltype == list_type::list) {
				o + "@obj@.m_link_@prop@.values.emplace_back();";
			}
		} else if(io.index == index_type::none) {
			o + "@obj@.m_@prop@.values.emplace_back();";
		}
	}
	for(auto& cp : cob.properties) {
		o + substitute{ "prop", cp.name };
		if(cp.is_derived) {
		} else if(cp.type == property_type::bitfield) {
			o + "@obj@.m_@prop@.values.resize(1 + (@obj@.size_used + 8) / 8);";
		} else if(cp.type == property_type::special_vector) {
			o + "@obj@.m_@prop@.values.push_back(std::numeric_limits<dcon::stable_mk_2_tag>::max());";
		} else {
			o + "@obj@.m_@prop@.values.emplace_back();";
		}
	}
	o + "++@obj@.size_used;";

	return o;
}

basic_builder& increase_size(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name };

	for(auto& cr : cob.relationships_involved_in) {
		o + substitute{ "rel", cr.relation_name } +substitute{ "in_rel_as", cr.property_name };
		if(cr.as_primary_key) {
			o + "@rel@.size_used = @obj@.size_used + 1;";
		}
	}
	o + "++@obj@.size_used;";

	return o;
}

basic_builder& make_non_erasable_create(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name } + substitute{ "size", std::to_string(cob.size) };
	o + heading{ "container create for @obj@" };

	o + "@obj@_id create_@obj@()" + block{

		o + "@obj@_id new_id(@obj@_id::value_base_t(@obj@.size_used));";

		if(cob.is_expandable) {
			expandable_push_back(o, cob);
		} else {
			o + "if(@obj@.size_used >= @size@) std::abort();";
			increase_size(o, cob);
		}
		if(cob.hook_create)
			o + "on_create_@obj@(new_id);";
		o + "return new_id;";
	};

	o + line_break{};
	return o;
}

basic_builder& make_clearing_delete(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name } +substitute{ "size", std::to_string(cob.size) };
	o + heading{ "container delete for @obj@" };

	o + "void delete_@obj@(@obj@_id id_removed)" + block{
		if(cob.hook_delete)
			o + "on_delete_@obj@(id_removed);";

		for(auto& io : cob.indexed_objects) {
			if(cob.primary_key != io) {
				o + substitute{ "i_prop", io.property_name } +substitute{ "i_type", io.type_name };
				o + "@obj@_set_@i_prop@(id_removed, @i_type@_id());";
			}
		}
		for(auto& cr : cob.relationships_involved_in) {
			o + substitute{ "rel", cr.relation_name } +substitute{ "r_prop", cr.property_name };
			if(cr.as_primary_key) {
				o + "delete_@rel@(id_removed);";
			} else if(cr.indexed_as == index_type::at_most_one) {
				o + "@obj@_remove_@rel@_as_@r_prop@(id_removed));";
			} else if(cr.indexed_as == index_type::many) {
				o + "@obj@_remove_all_@rel@_as_@r_prop@(id_removed));";
			}
		}
		for(auto& cp : cob.properties) {
			if(!cp.is_derived) {
				clear_value(o, cob.name, cp.name, cp.data_type, cp.type, "id_removed");
			}
		}
	};

	o + line_break{};
	return o;
}

basic_builder& make_erasable_delete(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name } +substitute{ "size", std::to_string(cob.size) };
	o + heading{ "container delete for @obj@" };

	o + "void delete_@obj@(@obj@_id id_removed)" + block{
		o + "if(!is_valid_@@obj(id_removed)) return;";
		if(cob.hook_delete)
			o + "on_delete_@obj@(id_removed);";

		o + "@obj@.m__index.vptr()[id_removed.index()] = @obj@.first_free;"; 
		o + "@obj@.first_free = id_removed;";
		o + "if(@obj@.size_used - 1 == id_removed.index())" + block{
			o + "for( ; @obj@.size_used > 0 && "
			"@obj@.m__index.vptr()[@obj@.size_used - 1] != @obj@_id(@obj@_id::value_base_t(@obj@.size_used - 1));  "
			"--@obj@.size_used) ;";
		};

		for(auto& io : cob.indexed_objects) {
			if(cob.primary_key != io) {
				o + substitute{ "i_prop", io.property_name } +substitute{ "i_type", io.type_name };
				o + "@obj@_set_@i_prop@(id_removed, @i_type@_id());";
			}
		}
		for(auto& cr : cob.relationships_involved_in) {
			o + substitute{ "rel", cr.relation_name } +substitute{ "r_prop", cr.property_name };
			if(cr.as_primary_key) {
				o + "delete_@rel@(id_removed);";
				o + "@rel@.size_used = @obj@.size_used;";
			} else if(cr.indexed_as == index_type::at_most_one) {
				o + "@obj@_remove_@rel@_as_@r_prop@(id_removed));";
			} else if(cr.indexed_as == index_type::many) {
				o + "@obj@_remove_all_@rel@_as_@r_prop@(id_removed));";
			}
		}
		for(auto& cp : cob.properties) {
			if(!cp.is_derived) {
				clear_value(o, cob.name, cp.name, cp.data_type, cp.type, "id_removed");
			}
		}
	};

	o + line_break{};
	return o;
}
basic_builder& erasable_set_new_id(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "t_obj", cob.name };
	if(!cob.is_expandable) {
		o + "if(!bool(@t_obj@.first_free)) std::abort();";
		o + "@t_obj@_id new_id = @t_obj@.first_free;";
		o + "@t_obj@.first_free = @t_obj@.m__index.vptr()[@t_obj@.first_free.index()];";
		o + "@t_obj@.m__index.vptr()[new_id.index()] = new_id;";
	} else {
		o + "@t_obj@_id new_id = @t_obj@.first_free;";
		o + "bool expanded = !bool(@t_obj@.first_free );";
		o + "if(expanded)" + block{
			o + "new_id = @t_obj@_id(@t_obj@_id::value_base_t(@t_obj@.size_used));";
			o + "@t_obj@.m__index.values.push_back(new_id);";
		} +append{ "else" } +block{
			o + "@t_obj@.first_free = @t_obj@.m__index.vptr()[@t_obj@.first_free.index()];";
			o + "@t_obj@.m__index.vptr()[new_id.index()] = new_id;";
		};
	}
	o + "@t_obj@.size_used = std::max(@t_obj@.size_used, uint32_t(new_id.index() + 1));";
	for(auto& cr : cob.relationships_involved_in) {
		if(cr.as_primary_key) {
			o + substitute{ "rel", cr.relation_name };
			o + "@rel@.size_used = @t_obj@.size_used;";
		}
	}
	if(cob.is_expandable) {
		o + "if(expanded)" + block{
			expandable_push_back(o, cob);
		};
	}

	return o;
}


basic_builder& make_erasable_create(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name } +substitute{ "size", std::to_string(cob.size) };
	o + heading{ "container create for @obj@" };

	o + "@obj@_id create_@obj@()" + block{
		
		erasable_set_new_id(o, cob);

		if(cob.hook_create)
			o + "on_create_@obj@(new_id);";
		o + "return new_id;";
	};

	o + line_break{};
	return o;
}

basic_builder& make_internal_move_relationship(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name } +substitute{ "size", std::to_string(cob.size) };
	o + heading{ "container move relationship for @obj@" };

	o + "void internal_move_relationship_@obj@(@obj@_id last_id, @obj@_id id_removed)" + block{
		for(auto& io : cob.indexed_objects) {
			if(cob.primary_key != io) {
				o + substitute{ "i_prop", io.property_name } +substitute{ "i_type", io.type_name };
				o + "@obj@_set_@i_prop@(id_removed, @i_type@_id());";

				if(io.index == index_type::at_most_one) {
					o + "if(auto related = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(related))" + block{
						o + "@obj@.m_link_back_@i_prop@.vptr()[related.index()] = id_removed;";
					};
				} else if(io.index == index_type::many) {
					if(io.ltype == list_type::list) {
						o + "if(bool(@obj@.m_@i_prop@.vptr()[last_id.index()]))" + block{
							o + "auto tmp = @obj@.m_link_@i_prop@.vptr()[last_id.index()];";
							o + "if(bool(tmp.left))" + block{
								o + "@obj@.m_link_@i_prop@.vptr()[tmp.left.index()].right = id_removed;";
							} +append{ "else" } +block{
								o + "for(auto lpos = last_id; bool(lpos); lpos = @obj@.m_link_@i_prop@.vptr()[lpos.index()].right)" + block{
									o + "@obj@.m_head_back_@i_prop@.vptr()[@obj@.m_@i_prop@.vptr()[lpos.index()].index()] = id_removed;";
								};
							};
							o + "if(bool(tmp.right))" + block{
								o + "@obj@.m_link_@i_prop@.vptr()[tmp.right.index()].left = id_removed;";
							};
						};
						move_value(o, cob.name, std::string("link_") + io.property_name,
							"last_id", "id_removed", cob.name + "_id_pair()");
					} else if(io.ltype == list_type::std_vector) {
						o + "if(auto tmp = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(tmp))" + block{
							o + "auto& vref = @obj@.m_array_@i_prop@.vptr()[tmp.index()];";
							o + "if(auto pos = std::find(vref.begin(), vref.end(), last_id); pos != vref.end())" + block {
								o + "*pos = id_removed;";
							};
						};
					} else if(io.ltype == list_type::array) {
						o + "if(auto tmp = @obj@.m_@i_prop@.vptr()[last_id.index()]; bool(tmp))" + block{
							o + "dcon::replace_unique_item(@obj@.@i_prop@_storage, @obj@.m_array_@i_prop@.vptr()[tmp.index()],"
								" last_id, id_removed);";
						};
					}
				}

				move_value(o, cob.name, io.property_name,
					"last_id", "id_removed", io.type_name + "_id()");
			}
		}
		for(auto& cp : cob.properties) {
			o + substitute{ "prop", cp.name };
			if(cp.is_derived) {
			} else if(cp.type == property_type::special_vector) {
				o + "@obj@.@prop@_storage.release(@obj@.m_@prop@.vptr()[id_removed.index()]);";
				move_value(o, cob.name, cp.name,
					"last_id", "id_removed", "std::numeric_limits<dcon::stable_mk_2_tag>::max()");
			} else if(cp.type == property_type::bitfield) {
				o + "dcon::bit_vector_set(@obj@.m_@prop@.vptr(), id_removed.index(), "
					"dcon::bit_vector_get(@obj@.m_@prop@.vptr(), last_id.index()));";
				o + "dcon::bit_vector_set(@obj@.m_@prop@.vptr(), last_id.index(), false);";
			} else {
				move_value(o, cob.name, cp.name,
					"last_id", "id_removed", cp.data_type + "{}");
			}
		}
	};

	o + line_break{};
	return o;
}

basic_builder& make_relation_try_create(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name } +substitute{ "size", std::to_string(cob.size) }
	+substitute{ "params", make_relationship_parameters(cob) };
	o + heading{ "container try create relationship for @obj@" };

	o + "@obj@_id try_create_@obj@(@params@)" + block{
		for(auto& iob : cob.indexed_objects) {
			o + substitute{ "prop", iob.property_name };
			if(cob.primary_key != iob) {
				if(iob.index == index_type::at_most_one) {
					o + "if(bool(@prop@_p) && bool(@obj@.m_link_@prop@.vptr()[@prop@_p.index()])) return @obj@_id();";
				}
			} else {
				o + "if(is_valid_@obj@(@prop@_p)) return @obj@_id();";
			}
		}

		if(cob.primary_key.points_to != nullptr) {
			for(auto& iob : cob.indexed_objects) {
				if(cob.primary_key == iob) {
					o + substitute{ "pkey", iob.property_name };
				}
			}
			o + "@obj@_id new_id = @pkey@_p;";
		} else if(cob.store_type != storage_type::erasable) {
			o + "@obj@_id new_id = @obj@_id(@obj@_id::value_base_t(@obj@.size_used));";

			if(!cob.is_expandable) {
				o + "if@obj@.size_used >= @size@) std::abort();";
				increase_size(o, cob);
			} else {
				expandable_push_back(o, cob);
			}
		} else {
			erasable_set_new_id(o, cob);
		}
		

		for(auto& iob : cob.indexed_objects) {
			o + substitute{ "prop", iob.property_name };
			if( cob.primary_key != iob)  {
				o + "@obj@_set_@prop@(new_id, @prop@_p);";
			}
		}

		if(cob.hook_create)
			o + "on_create_@obj@(new_id);";

		o + "return new_id;";
	};

	o + line_break{};
	return o;
}
basic_builder& make_relation_force_create(basic_builder& o, relationship_object_def const& cob) {
	o + substitute{ "obj", cob.name } +substitute{ "size", std::to_string(cob.size) }
	+substitute{ "params", make_relationship_parameters(cob) };
	o + heading{ "container force create relationship for @obj@" };

	o + "@obj@_id force_create_@obj@(@params@)" + block{
		if(cob.primary_key.points_to != nullptr) {
			for(auto& iob : cob.indexed_objects) {
				if(cob.primary_key == iob) {
					o + substitute{ "pkey", iob.property_name };
				}
			}
			o + "@obj@_id new_id = @pkey@_p;";
		} else if(cob.store_type != storage_type::erasable) {
			o + "@obj@_id new_id = @obj@_id(@obj@_id::value_base_t(@obj@.size_used));";

			if(!cob.is_expandable) {
				o + "if@obj@.size_used >= @size@) std::abort();";
				increase_size(o, cob);
			} else {
				expandable_push_back(o, cob);
			}
		} else {
			erasable_set_new_id(o, cob);
		}


		for(auto& iob : cob.indexed_objects) {
			o + substitute{ "prop", iob.property_name };
			if( cob.primary_key != iob) {
				o + "@obj@_set_@prop@(new_id, @prop@_p);";
			}
		}

		if(cob.hook_create)
			o + "on_create_@obj@(new_id);";

		o + "return new_id;";
	};

	o + line_break{};
	return o;
}

basic_builder& make_serialize_plan_generator(basic_builder& o, file_def const& parsed_file, load_save_def const& rt) {

	o + substitute{"rt_name", rt.name};

	o + "load_record make_serialize_record_@rt_name@() const noexcept" + block{
		o + "load_record result;";

		for(auto& ob : parsed_file.relationship_objects) {
			o + substitute{ "obj", ob.name };

			
			bool matched_obj_tag = false;
			for(auto& tg : rt.obj_tags) {
				matched_obj_tag = matched_obj_tag || (std::find(ob.obj_tags.begin(), ob.obj_tags.end(), tg) != ob.obj_tags.end());
			}
			if((matched_obj_tag && rt.objects_filter == filter_type::include) || 
				(!matched_obj_tag && rt.objects_filter != filter_type::include)) {

				o + "result.@obj@ = true;";
				for(auto& iob : ob.indexed_objects) {
					o + substitute{ "prop", iob.property_name };
					o + "result.@obj@_@prop@ = true;";

				}
				if(ob.store_type == storage_type::erasable) {
					o + "result.@obj@__index = true;";
				}
				for(auto& prop : ob.properties) {
					bool matched_prop_tag = false;
					for(auto& tg : rt.property_tags) {
						matched_prop_tag = matched_prop_tag
							|| (std::find(prop.property_tags.begin(), prop.property_tags.end(), tg) != prop.property_tags.end());
					}

					if((matched_prop_tag && rt.properties_filter == filter_type::include) ||
						(!matched_prop_tag && rt.properties_filter != filter_type::include)) {
						o + substitute{ "prop", prop.name };
						o + "result.@obj@_@prop@ = true;";
					}
				}
			}
		}
		o + "return result;";
	};

	o + line_break{};
	return o;
}

basic_builder& make_serialize_size(basic_builder& o, file_def const& parsed_file) {
	o + heading{ "calculate size (in bytes) required to serialize the desired objects, relationships, and properties" };

	o + "uint64_t serialize_size(load_record const& serialize_selection) const" + block{
		o + "uint64_t total_size = 0;";

		for(auto& ob : parsed_file.relationship_objects) {
			o + substitute{ "obj", ob.name };

			o + "if(serialize_selection.@obj@)" + block{
				o + "dcon::record_header header(sizeof(uint32_t), \"uint32_t\", \"@obj@\", \"$size\");";
				o + "total_size += header.serialize_size();";
				o + "total_size += sizeof(uint32_t);";

				for(auto& iob : ob.indexed_objects) {
					o + substitute{ "prop", iob.property_name } +substitute{ "type", iob.type_name };
					o + substitute{ "key_backing", size_to_tag_type(iob.related_to->size) };
					if(ob.primary_key != iob) {
						o + "if(serialize_selection.@obj@_@prop@)" + block{
							o + "dcon::record_header header(sizeof(@type@_id) * @obj@.size_used, \"@key_backing@\", \"@obj@\", \"@prop@\");";
							o + "total_size += header.serialize_size();";
							o + "total_size += sizeof(@type@_id) * @obj@.size_used;";
						};
					}
				}

				if(ob.is_relationship) {
					o + "dcon::record_header headerb(0, \"$\", \"@obj@\", \"$index_end\");";
					o + "total_size += headerb.serialize_size();";
				}
			};

			if(ob.store_type == storage_type::erasable) {
				o + substitute{ "key_backing", size_to_tag_type(ob.size) };
				o + "if(serialize_selection.@obj@__index)" + block{
					o + "dcon::record_header header(sizeof(@obj@_id) * @obj@.size_used, \"@key_backing@\", \"@obj@\", \"_index\");";
					o + "total_size += header.serialize_size();";
					o + "total_size += sizeof(@obj@_id) * @obj@.size_used;";
				};
			}

			for(auto& prop : ob.properties) {
				o + substitute{ "prop", prop.name } +substitute{ "type", prop.data_type };
				if(prop.is_derived) {

				} else if(prop.type == property_type::bitfield) {
					o + "if(serialize_selection.@obj@_@prop@)" + block{
						o + "dcon::record_header header((@obj@.size_used + 7) / 8, \"bitfield\", \"@obj@\", \"@prop@\");";
						o + "total_size += header.serialize_size();";
						o + "total_size += (@obj@.size_used + 7) / 8;";
					};
				} else if(prop.type == property_type::special_vector) {
					o + substitute{ "vtype_name_sz", std::to_string(prop.data_type.length() + 1) };
					o + "if(serialize_selection.@obj@_@prop@)" + block{
						o + "std::for_each(@obj@.m_@prop@.vptr(), @obj@.m_@prop@.vptr() @obj@.size_used, [t = this, &total_size](stable_mk_2_tag obj)" + block{
							o + "auto rng = dcon::get_range(t->@obj@.@prop@_storage, obj);";
							o + "total_size += sizeof(uint16_t);";
							o + "total_size += sizeof(@type@) * (rng.second - rng.first);";
						} +append{");"};
						
						o + "total_size += @vtype_name_sz@;";
						o + "dcon::record_header header(total_size, \"stable_mk_2_tag\", \"@obj@\", \"@prop@\");";
						o + "total_size += header.serialize_size();";
					};
				} else if(prop.type == property_type::object) {
					o + "if(serialize_selection.@obj@_@prop@)" + block{
						o + "std::for_each(@obj@.m_@prop@.vptr(), @obj@.m_@prop@.vptr() @obj@.size_used, "
						"[t = this, &total_size](@type@ const& obj){ total_size += t->serialize_size(obj); });";
						o + "dcon::record_header header(total_size, \"@type@\", \"@obj@\", \"@prop@\");";
						o + "total_size += header.serialize_size();";
					};
				} else {
					o + "if(serialize_selection.@obj@_@prop@)" + block{
						o + "dcon::record_header header(sizeof(@type@) * @obj@.size_used, \"@type@\", \"@obj@\", \"@prop@\");";
						o + "total_size += header.serialize_size();";
						o + "total_size += sizeof(@type@) * @obj@.size_used;";
					};
				}
			}
		}
		o + "return total_size;";
	};
	
	o + line_break{};
	return o;
}

basic_builder& make_serialize(basic_builder& o, file_def const& parsed_file) {
	o + heading{ "serialize the desired objects, relationships, and properties" };

	//serialize
	o + "void serialize(std::byte*& output_buffer, load_record const& serialize_selection) const" + block{
		for(auto& ob : parsed_file.relationship_objects) {
			o + substitute{"obj", ob.name};
			o + "if(serialize_selection.@obj@)" + block{
				o + "dcon::record_header header(sizeof(uint32_t), \"uint32_t\", \"@obj@\", \"$size\");";
				o + "header.serialize(output_buffer);";
				o + "*(reinterpret_cast<uint32_t*>(output_buffer)) = @obj@.size_used;";
				o + "output_buffer += sizeof(uint32_t);";

				for(auto& iob : ob.indexed_objects) {
					if(ob.primary_key != iob) {
						o + substitute{ "prop", iob.property_name } +substitute{ "type", iob.type_name };
						o + substitute{ "u_type", size_to_tag_type(iob.related_to->size) };

						o + inline_block{
							o + "dcon::record_header header(sizeof(@type@_id) * @obj@.size_used, \"@u_type@\", \"@obj@\", \"@prop@\");";
							o + "header.serialize(output_buffer);";
							o + "memcpy(reinterpret_cast<@type@_id*>(output_buffer), @obj@.m_@prop@.vptr(), sizeof(@type@_id) * @obj@.size_used);";
							o + "output_buffer += sizeof(@type@_id) * @obj@.size_used;";
						};
					}
				}

				if(ob.is_relationship) {
					o + "dcon::record_header headerb(0, \"$\", \"@obj@\", \"$index_end\");";
					o + "headerb.serialize(output_buffer);";
				}
			};

			if(ob.store_type == storage_type::erasable) {
				o + substitute{ "u_type", size_to_tag_type(ob.size) };
				o + "if(serialize_selection.@obj@__index)" + block{
					o + "dcon::record_header header(sizeof(@obj@_id) * @obj@.size_used, \"@u_type@\", \"@obj@\", \"_index\");";
					o + "header.serialize(output_buffer);";
					o + "memcpy(reinterpret_cast<@obj@_id*>(output_buffer), @obj@.m__index.vptr(), sizeof(@obj@_id) * @obj@.size_used);";
					o + "output_buffer += sizeof(@obj@_id) * @obj@.size_used;";
				};
			}

			

			for(auto& prop : ob.properties) {
				o + substitute{ "prop", prop.name } +substitute{ "type", prop.data_type };
				if(prop.is_derived) {

				} else if(prop.type == property_type::bitfield) {
					o + "if(serialize_selection.@obj@_@prop@)" + block{
						o + "dcon::record_header header((@obj@.size_used + 7) / 8, \"bitfield\", \"@obj@\", \"@prop@\");";
						o + "header.serialize(output_buffer);";
						o + "memcpy(reinterpret_cast<bitfield_type*>(output_buffer), @obj@.m_@prop@.vptr(), (@obj@.size_used + 7) / 8);";
						o + "output_buffer += (@obj@.size_used + 7) / 8;";
					};
				} else if(prop.type == property_type::special_vector) {
					o + "if(serialize_selection.@obj@_@prop@)" + block{
						o + "size_t total_size = 0;";
						o + "std::for_each(@obj@.m_@prop@.vptr(), @obj@.m_@prop@.vptr() @obj@.size_used, [t = this, &total_size](stable_mk_2_tag obj)" + block{
							o + "auto rng = dcon::get_range(t->@obj@.@prop@_storage, obj);";
							o + "total_size += sizeof(uint16_t) + sizeof(@type@) * (rng.second - rng.first);";
						} +append{");"};
						
						o + substitute{ "vname_sz", std::to_string(prop.data_type.length() + 1) };
						o + "total_size += @vname_sz@;";

						o + "dcon::record_header header(total_size, \"stable_mk_2_tag\", \"@obj@\", \"@prop@\");";
						o + "header.serialize(output_buffer);";

						o + "memcpy(reinterpret_cast<char*>(output_buffer), \"@type@\", @vname_sz@);";
						o + "output_buffer += @vname_sz@;";

						o + "std::for_each(@obj@.m_@prop@.vptr(), @obj@.m_@prop@.vptr() @obj@.size_used, [t = this, output_buffer](stable_mk_2_tag obj)" + block{
							o + "auto rng = dcon::get_range(t->@obj@.@prop@_storage, obj);";
							o + "*(reinterpret_cast<uint16_t*>(output_buffer)) = uint16_t(rng.second - rng.first);";
							o + "output_buffer += sizeof(uint16_t);";
							o + "memcpy(reinterpret_cast<@type@*>(output_buffer), rng.first, sizeof(@type@) * (rng.second - rng.first));";
							o + "output_buffer += sizeof(@type@) * (rng.second - rng.first);";
						} +append{");"};
					};
					
				} else if(prop.type == property_type::object) {
					o + "if(serialize_selection.@obj@_@prop@)" + block{
						o + "size_t total_size = 0;";
						o + "std::for_each(@obj@.m_@prop@.vptr(), @obj@.m_@prop@.vptr() @obj@.size_used, [t = this, &total_size](@type@ const& obj)" + block{
							o + "total_size += t->serialize_size(obj);";
						} +append{");"};
						o + "dcon::record_header header(total_size, \"@type@\", \"@obj@\", \"@prop@\");";
						o + "header.serialize(output_buffer);";
						o + "std::for_each(@obj@.m_@prop@.vptr(), @obj@.m_@prop@.vptr() + @obj@.size_used, "
							"[t = this, &output_buffer](@type@ const& obj){ t->serialize(output_buffer, obj); });";
					};
				} else {
					o + "if(serialize_selection.@obj@_@prop@)" + block{
						o + "dcon::record_header header(sizeof(@type@) * @obj@.size_used, \"@type@\", \"@obj@\", \"@prop@\");";
						o + "header.serialize(output_buffer);";
						o + "memcpy(reinterpret_cast<@type@*>(output_buffer), @obj@.m_@prop@.vptr(), sizeof(@type@) * @obj@.size_used);";
						o + "output_buffer += sizeof(@type@) * @obj@.size_used;";
					};
				}
			}
		}
	};

	o + line_break{};
	return o;
}

basic_builder& wrong_type_cast(basic_builder& o, std::string const& wrong_type, std::string const& prop_name,
	std::string const& prop_type, bool cast_to_value = true) {
	o + substitute{"w_type", wrong_type };
	o + substitute{ "w_prop", prop_name };
	o + substitute{ "c_type", prop_type };
	o + "else if(header.is_type(\"@w_type@\"))" + block{
		o + "for(uint32_t i = 0; i < std::min(@obj@.size_used, uint32_t(header.record_size / sizeof(@w_type@))); ++i)" + block{
			if(cast_to_value)
				o + "@obj@.m_@w_prop@.vptr()[i].value = @c_type@(*(reinterpret_cast<@w_type@ const*>(input_buffer) + i));";
			else
				o + "@obj@.m_@w_prop@.vptr()[i] = @c_type@(*(reinterpret_cast<@w_type@ const*>(input_buffer) + i));";
		};
		o + "serialize_selection.@obj@_@w_prop@ = true;";
	};
	return o;
}

basic_builder& conversion_attempt(basic_builder& o, file_def const& parsed_file, std::string const& target_type, bool to_bitfield = false) {
	for(auto& con : parsed_file.conversion_list) {
		if(con.to == target_type) {
			o + substitute{ "to_type", con.to };
			if(std::find(parsed_file.object_types.begin(), parsed_file.object_types.end(), con.from) != parsed_file.object_types.end()) {
				// from is an object
				o + substitute{ "f_type", con.from };
				o + "else if(header.is_type(\"@f_type@\"))" + block{
					o + "std::byte const* icpy = input_buffer;";
					o + "for(uint32_t i = 0; i < std::min(@obj@.size_used, uint32_t(header.record_size / sizeof(@f_type@))); ++i)" + block{
						o + "@f_type@ temp;";
						o + "deserialize(icpy, temp, input_buffer + header.record_size);";
						if(to_bitfield)
							o + "dcon::bit_vector_set(@obj@.m_@prop@.vptr(), i, convert_type(&temp, (@to_type@*)nullptr));";
						else
							o + "@obj@.m_@prop@.vptr()[i] = convert_type(&temp, (@to_type@*)nullptr));";
					};
					o + "serialize_selection.@obj@_@prop@ = true;";
				};
				
				o + "\t}";
			} else {
				// from not an object
				o + "else if(header.is_type(\"@f_type@\"))" + block{
					o + "for(uint32_t i = 0; i < std::min(@obj@.size_used, uint32_t(header.record_size / sizeof(@f_type@))); ++i)" + block{
						if(to_bitfield)
							o + "dcon::bit_vector_set(@obj@.m_@prop@.vptr(), i, "
								"convert_type(reinterpret_cast<@f_type@ const*>(input_buffer) + i, (@to_type@*)nullptr));";
						else
							o + "@obj@.m_@prop@.vptr()[i] = convert_type(reinterpret_cast<@f_type@ const*>(input_buffer) + i, (@to_type@*)nullptr));";
					};
					o + "serialize_selection.@obj@_@prop@ = true;";
				};
			}
		}
	}
	return o;
}

basic_builder& make_deserialize(basic_builder& o, file_def const& parsed_file, bool with_mask) {
	if(with_mask)
		o + heading{ "deserialize the desired objects, relationships, and properties where the mask is set" };
	else
		o + heading{ "deserialize the desired objects, relationships, and properties" };

	o + substitute{ "mask_param", with_mask ? ", load_record const& mask" : ""};
	o + "void deserialize(std::byte const*& input_buffer, std::byte const* end, load_record& serialize_selection@mask_param@)" + block{
		o + "while(input_buffer < end)" + block{
			o + "dcon::record_header header;";
			o + "header.deserialize(input_buffer, end);";
			o + "if(input_buffer + header.record_size <= end)" + block{ // wrap: gaurantee enough space to read entire buffer

				bool first_header_if = true;
				for(auto& ob : parsed_file.relationship_objects) {
					o + substitute{ "obj", ob.name } +substitute{ "obj_sz", std::to_string(ob.size) }
					+ substitute{ "mcon", with_mask ? std::string(" && mask.") + ob.name : std::string() };
					if(first_header_if)
						first_header_if = false;
					else
						o + append{ "else" };

					o + "if(header.is_object(\"@obj@\")@mcon@)" + block{ //has matched object
						o + "if(header.is_property(\"$size\") && header.record_size == sizeof(uint32_t))" + block{
							o + "@obj@_resize(*(reinterpret_cast<uint32_t const*>(input_buffer)));";
							o + "serialize_selection.@obj@ = true;";
						};
						if(ob.store_type == storage_type::erasable) {
							o + substitute{"u_type" , size_to_tag_type(ob.size) };
							o + substitute{ "mcon", with_mask ? std::string(" && mask.") + ob.name + "__index" : std::string() };
							o + "else if(header.is_property(\"__index\")@mcon@)" + block{
								o + "if(header.is_type(\"@u_type@\"))" + block{
									o + "memcpy(@obj@.m__index.vptr(), reinterpret_cast<@u_type@ const*>(input_buffer), "
										"std::min(size_t(@obj@.size_used) * sizeof(@u_type@), header.record_size));";
									o + "serialize_selection.@obj@__index = true;";
								};
								if(size_to_tag_type(ob.size) != "uint8_t") {
									wrong_type_cast(o, "uint8_t", "_index", size_to_tag_type(ob.size), true);
								}
								if(size_to_tag_type(ob.size) != "uint16_t") {
									wrong_type_cast(o, "uint16_t", "_index", size_to_tag_type(ob.size), true);
								}
								if(size_to_tag_type(ob.size) != "uint32_t") {
									wrong_type_cast(o, "uint32_t", "_index", size_to_tag_type(ob.size), true);
								}

								//redo free list
								o + "if(serialize_selection.@obj@__index == true)" + block{
									o + "@obj@.first_free = @obj@_id();";
									o + "for(int32_t j = @obj_sz@; j >= 0; --j)" + block{
										o + "if(@obj@.m__index.vptr()[j] != @obj@_id(@u_type@(j)))" + block{
											o + "@obj@.m__index.vptr()[j] = @obj@.first_free;";
											o + "@obj@.first_free = @obj@_id(@u_type@(j));";
										};
									};
								};
							};
						} // end: load index handling for erasable

						for(auto& iob : ob.indexed_objects) {
							if(ob.primary_key != iob) {
								o + substitute{ "prop", iob.property_name } +substitute{ "ob_u_type", size_to_tag_type(ob.size) };
								o + substitute{ "mcon", with_mask ? std::string(" && mask.") + ob.name + "_" + iob.property_name : std::string() };
								o + substitute{ "u_type" , size_to_tag_type(iob.related_to->size) };

								o + "else if(header.is_property(\"@prop@\")@mcon@)" + block{
									o + "if(header.is_type(\"@u_type@\"))" + block{
										o + "memcpy(@obj@.m_@prop@.vptr(), reinterpret_cast<@u_type@ const*>(input_buffer), "
											"std::min(size_t(@obj@.size_used) * sizeof(@u_type@), header.record_size));";
										o + "serialize_selection.@obj@_@prop@ = true;";
									};
									if(size_to_tag_type(iob.related_to->size) != "uint8_t") {
										wrong_type_cast(o, "uint8_t", iob.property_name, size_to_tag_type(iob.related_to->size), true);
									}
									if(size_to_tag_type(iob.related_to->size) != "uint16_t") {
										wrong_type_cast(o, "uint16_t", iob.property_name, size_to_tag_type(iob.related_to->size), true);
									}
									if(size_to_tag_type(iob.related_to->size) != "uint32_t") {
										wrong_type_cast(o, "uint32_t", iob.property_name, size_to_tag_type(iob.related_to->size), true);
									}
								};
							}
						} // end index properties

						if(ob.is_relationship) {
							o + substitute{ "mcon", with_mask ? std::string(" && mask.") + ob.name : std::string() };
							o + "else if(header.is_property(\"$index_end\")@mcon@)" + block{
								for(auto& iob : ob.indexed_objects) {
									if(ob.primary_key != iob) {
										o + substitute{ "prop", iob.property_name } +substitute{ "type", iob.type_name };
										o + "if(serialize_selection.@obj@_@prop@ == true)" + block{
											o + "for(uint32_t i = 0; i < @obj@.size_used; ++i)" + block{
												o + "auto tmp = @obj@.m_@prop@.vptr()[i];";
												o + "@obj@.m_@prop@.vptr()[i] = @type@_id();";
												o + "@obj@_set_@prop@(@obj@_id(@obj@_id::value_base_t(i)), tmp);";
											};
										};
									}
								}
							};
						}

						for(auto& prop : ob.properties) {
							o + substitute{ "prop", prop.name } +substitute{ "type", prop.data_type };
							o + substitute{ "mcon", with_mask ? std::string(" && mask.") + ob.name + "_" + prop.name : std::string() };
							o + "else if(header.is_property(\"@prop@\")@mcon@)" + block{
								if(prop.is_derived) {

								} else if(prop.type == property_type::bitfield) {
									o + "if(header.is_type(\"bitfield\"))" + block{
										o + "memcpy(@obj@.m_@prop@.vptr(), reinterpret_cast<bitfield_type const*>(input_buffer)"
											", std::min(size_t(@obj@.size_used + 7) / 8, header.record_size));";
										o + "serialize_selection.@obj@_@prop@ = true;";
									};
									conversion_attempt(o, parsed_file, "bool", true);
								} else if(prop.type == property_type::special_vector) {
									o + "if(header.is_type(\"stable_mk_2_tag\"))" + block{
										o + "uint32_t ix = 0;";
									//read internal data type
									o + "std::byte const* zero_pos = std::find(input_buffer, input_buffer + header.record_size, 0);";

									o + "if(dcon::char_span_equals_str(reinterpret_cast<char const*>(input_buffer), "
										"reinterpret_cast<char const*>(zero_pos), \"@type@\"))" + block{ //correct type

										o + "for(std::byte const* icpy = zero_pos + 1; ix < @obj@.size_used && icpy < input_buffer + header.record_size; )" + block{
											o + "uint16_t sz = 0;";

											o + "if(icpy + sizeof(uint16_t) <= input_buffer + header.record_size)" + block{
												o + "sz = uint16_t(std::min(size_t(*(reinterpret_cast<uint16_t const*>(icpy))), "
													"(input_buffer + header.record_size - (icpy + sizeof(uint16_t))) / sizeof(@type@) ));";
												o + "icpy += sizeof(uint16_t);";
											};
											o + "dcon::load_range(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[ix], "
												"reinterpret_cast<@type@ const*>(icpy), reinterpret_cast<@type@ const*>(icpy) + sz)";
											o + "icpy += sz * sizeof(@type@);";
											o + "++ix;";
										};
									}; // end correct type
									for(auto& con : parsed_file.conversion_list) {
										if(con.to == prop.data_type) {
											o + substitute{ "f_type", con.from };
											if(std::find(parsed_file.object_types.begin(), parsed_file.object_types.end(), con.from) != parsed_file.object_types.end()) {
												// from is an object
												o + "else if(dcon::char_span_equals_str(reinterpret_cast<char const*>(input_buffer), "
													"reinterpret_cast<char const*>(zero_pos), \"@f_type@\"))" + block{

													o + "for(std::byte const* icpy = zero_pos + 1; ix < @obj@.size_used && icpy < input_buffer + header.record_size; )" + block{
														o + "uint16_t sz = 0;";
														o + "if(icpy + sizeof(uint16_t) <= input_buffer + header.record_size)" + block{
															o + "sz = uint16_t(std::min(size_t(*(reinterpret_cast<uint16_t const*>(icpy))), "
																"(input_buffer + header.record_size - (icpy + sizeof(uint16_t))) / sizeof(@f_type@) ));";
															o + "icpy += sizeof(uint16_t);";
														};

														o + "dcon::resize(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[ix], sz);";
														o + "for(uint32_t ii = 0; ii < sz && icpy < input_buffer + header.record_size; ++ii)" + block{
															o + "@f_type@ temp;";
															o + "deserialize(icpy, temp, input_buffer + header.record_size);";
															o + "dcon::get(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[ix], ii) = convert_type(&temp, (@type@*)nullptr);";
														};
														o + "++ix;";
													};
													o + "serialize_selection.@obj@_@prop@ = true;";
												};
											} else {
												// from not an object
												o + "else if(dcon::char_span_equals_str(reinterpret_cast<char const*>(input_buffer), "
													"reinterpret_cast<char const*>(zero_pos), \"@f_type@\"))" + block{ // if = from

													o + "for(std::byte const* icpy = zero_pos + 1; ix < @obj@.size_used && icpy < input_buffer + header.record_size; )" + block {
														o + "uint16_t sz = 0;";
														o + "if(icpy + sizeof(uint16_t) <= input_buffer + header.record_size)" + block{
															o + "sz = uint16_t(std::min(size_t(*(reinterpret_cast<uint16_t const*>(icpy))), "
																"(input_buffer + header.record_size - (icpy + sizeof(uint16_t))) / sizeof(@f_type@) ));";
															o + "icpy += sizeof(uint16_t);";
														};

														o + "dcon::resize(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[ix], sz);";
														o + "for(uint32_t ii = 0; ii < sz && icpy < input_buffer + header.record_size; ++ii)" + block{
															o + "dcon::get(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[ix], ii) = "
																"convert_type(reinterpret_cast<@f_type@ const*>(icpy), (@type@*)nullptr);";
															o + "icpy += sizeof(@f_type@);";
														};
														o + "++ix;";
													};
													o + "serialize_selection.@obj@_@prop@ = true;";
												};

											}
										}
									} // end for each in conversion list
									if(is_common_type(prop.data_type)) {
										const auto normed_type = normalize_type(prop.data_type);
										for(auto& basic_type : common_types) {
											if(basic_type != normed_type) {
												o + substitute{"b_type", basic_type };
												o + "else if(dcon::char_span_equals_str(reinterpret_cast<char const*>(input_buffer), "
													"reinterpret_cast<char const*>(zero_pos), \"@b_type@\"))" + block{

													o + "for(std::byte const* icpy = zero_pos + 1; ix < @obj@.size_used && icpy < input_buffer + header.record_size; )" + block{
														o + "uint16_t sz = 0;";
														o + "if(icpy + sizeof(uint16_t) <= input_buffer + header.record_size)" + block{
															o + "sz = uint16_t(std::min(size_t(*(reinterpret_cast<uint16_t const*>(icpy))), "
																"(input_buffer + header.record_size - (icpy + sizeof(uint16_t))) / sizeof(@b_type@) ));";
															o + "icpy += sizeof(uint16_t);";
														};

														o + "dcon::resize(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[ix], sz);";
														o + "for(uint32_t ii = 0; ii < sz && icpy < input_buffer + header.record_size; ++ii)" + block{
															o + "dcon::get(@obj@.@prop@_storage, @obj@.m_@prop@.vptr()[ix], ii) = @type@(*(reinterpret_cast<@b_type@ const*>(icpy) + i));";
															o + "icpy += sizeof(@b_type@);";
														};
														o + "++ix;";
													};
													o + "serialize_selection.@obj@_@prop@ = true;";
												};
											}
										}
									}
								};
									// end if prop is special array
								} else if(prop.type == property_type::object) {
									o + "if(header.is_type(\"@type@\"))" + block{
										o + "std::byte const* icpy = input_buffer;";
										o + "for(uint32_t i = 0; icpy < input_buffer + header.record_size && i < @obj@.size_used; ++i)" + block{
											o + "deserialize(icpy, @obj@.m_@prop@.vptr()[i], input_buffer + header.record_size);";
										};
										o + "serialize_selection.@obj@_@prop@ = true;";
									};
									conversion_attempt(o, parsed_file, prop.data_type, false);
								} else {
									o + "if(header.is_type(\"@type@\"))" + block{
										o + "memcpy(@obj@.m_@prop@.vptr(), reinterpret_cast<@type@ const*>(input_buffer)"
											", std::min(size_t(@obj@.size_used) * sizeof(@type@), header.record_size));";
										o + "serialize_selection.@obj@_@prop@ = true;";
									};
									conversion_attempt(o, parsed_file, prop.data_type, false);
									if(is_common_type(prop.data_type)) {
										const auto normed_type = normalize_type(prop.data_type);
										for(auto& basic_type : common_types) {
											if(basic_type != normed_type) {
												wrong_type_cast(o, basic_type, prop.name, prop.data_type, false);
											}
										}
									}
								} // end prop type cases
							}; // end header property == property type" in output 
						} // end loop over object properties 
					}; // end "header object == object type" in output
				} // end loop over object and relation types
			}; // end if ensuring that buffer has enough space to read entire record
			o + "input_buffer += header.record_size;";
		};
	};

	o + line_break{};
	return o;
}



basic_builder& make_relation_many_join_getters_setters(basic_builder& o, std::string const& property_name,
	std::string const& containter_type, std::string const& relation_name,
	std::string const& name_in_relation) {

	o + substitute{ "obj", containter_type } +substitute{ "rel", relation_name }
	+substitute{ "prop", property_name } +substitute{ "as_name", name_in_relation };

	o + "template<typename T>";
	o + "void @obj@_for_each_@prop@_from_@rel@(@obj@_id id, T&& func) const" + block{
		o + "@obj@_for_each_@rel@_as_@as_name@(id, [&](@rel@_id i)" + block{
			o + "func(@rel@_get_@prop@(i));";
		} +append{");"};
	};

	return o;
}

basic_builder& make_relation_unique_non_pk_join_getters_setters(basic_builder& o, std::string const& property_name,
	std::string const& data_type, std::string const& containter_type, std::string const& relation_name,
	std::string const& name_in_relation, property_type ptype, bool relation_is_expandable) {
	
	o + substitute{"obj", containter_type } +substitute{ "rel", relation_name }
	+substitute{ "prop", property_name } +substitute{ "type", data_type } +substitute{ "as_name", name_in_relation };
	o + substitute{ "vector_position", relation_is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" };

	if(ptype == property_type::bitfield) {
		o + "bool @obj@_get_@prop@_from_@rel@(@obj@_id id) const" + block{
			o + "if(auto ref_id = @rel@.m_link_@as_name@.vptr()[id.index]; bool(ref_id))" + block{
				o + "return @rel@_get_@prop@(ref_id);";
			} +append{ "else" } +block{
				o + "return false;";
			};
		};
		o + "ve::vbitfield_type @obj@_get_@prop@_from_@rel@(@vector_position@<@obj@_id> id) const" + block{
			o + "auto ref_id = ve::load(id, @rel@.m_link_@as_name@.vptr())";
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::vbitfield_type @obj@_get_@prop@_from_@rel@(ve::partial_contiguous_tags<@obj@_id> id) const" + block{
			o + "auto ref_id = ve::load(id, @rel@.m_link_@as_name@.vptr())";
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::vbitfield_type @obj@_get_@prop@_from_@rel@(ve::tagged_vector<@obj@_id> id) const" + block{
			o + "auto ref_id = ve::load(id, @rel@.m_link_@as_name@.vptr())";
			o + "return @rel@_get_@prop@(ref_id);";
		};

		o + "void @obj@_set_@prop@_from_@rel@(@obj@_id id, bool val)" + block{
			o + "if(auto ref_id = @rel@.m_link_@as_name@.vptr()[id.index]; bool(ref_id))" + block{
				o + "@rel@_set_@prop@(ref_id, val);";
			};
		};
	} else if(ptype == property_type::object) {
		
	} else if(ptype == property_type::special_vector) {

	} else if(ptype == property_type::vectorizable) {
		o + "@type@ @obj@_get_@prop@_from_@rel@(@obj@_id id) const" + block{
			o + "if(auto ref_id = @rel@.m_link_@as_name@.vptr()[id.index]; bool(ref_id))" + block{
				o + "return @rel@_get_@prop@(ref_id);";
			} +append{ "else" } +block{
				o + "return @type@{};";
			};
		};
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@_from_@rel@(@vector_position@<@obj@_id> id) const" + block{
			o + "auto ref_id = ve::load(id, @rel@.m_link_@as_name@.vptr())";
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@_from_@rel@(ve::partial_contiguous_tags<@obj@_id> id) const" + block{
			o + "auto ref_id = ve::load(id, @rel@.m_link_@as_name@.vptr())";
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@_from_@rel@(ve::tagged_vector<@obj@_id> id) const" + block{
			o + "auto ref_id = ve::load(id, @rel@.m_link_@as_name@.vptr())";
			o + "return @rel@_get_@prop@(ref_id);";
		};

		o + "void @obj@_set_@prop@_from_@rel@(@obj@_id id, @type@ val)" + block{
			o + "if(auto ref_id = @rel@.m_link_@as_name@.vptr()[id.index]; bool(ref_id))" + block{
				o + "@rel@_set_@prop@(ref_id, val);";
			};
		};
	} else if(ptype == property_type::other) {
	}

	return o;
}

basic_builder& make_relation_unique_pk_join_getters_setters(basic_builder& o, std::string const& property_name,
	std::string const& data_type, std::string const& containter_type, std::string const& relation_name,
	std::string const& name_in_relation, property_type ptype, bool relation_is_expandable) {

	o + substitute{ "obj", containter_type } +substitute{ "rel", relation_name }
	+substitute{ "prop", property_name } +substitute{ "type", data_type } +substitute{ "as_name", name_in_relation };
	o + substitute{ "vector_position", relation_is_expandable ? "ve::unaligned_contiguous_tags" : "ve::contiguous_tags" };

	if(ptype == property_type::bitfield) {
		o + "bool @obj@_get_@prop@_from_@rel@(@obj@_id ref_id) const" + block{
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::vbitfield_type @obj@_get_@prop@_from_@rel@(@vector_position@<@obj@_id> ref_id) const" + block{
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::vbitfield_type @obj@_get_@prop@_from_@rel@(ve::partial_contiguous_tags<@obj@_id> ref_id) const" + block{
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::vbitfield_type @obj@_get_@prop@_from_@rel@(ve::tagged_vector<@obj@_id> ref_id) const" + block{
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "void @obj@_set_@prop@_from_@rel@(@obj@_id ref_id, bool val)" + block{
			o + "@rel@_set_@prop@(ref_id, val);";
		};
	} else if(ptype == property_type::object) {

	} else if(ptype == property_type::special_vector) {

	} else if(ptype == property_type::vectorizable) {
		o + "@type@ @obj@_get_@prop@_from_@rel@(@obj@_id ref_id) const" + block{
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@_from_@rel@(@vector_position@<@obj@_id> ref_id) const" + block{
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@_from_@rel@(ve::partial_contiguous_tags<@obj@_id> ref_id) const" + block{
			o + "return @rel@_get_@prop@(ref_id);";
		};
		o + "ve::value_to_vector_type<@type@> @obj@_get_@prop@_from_@rel@(ve::tagged_vector<@obj@_id> ref_id) const" + block{
			o + "return @rel@_get_@prop@(ref_id);";
		};

		o + "void @obj@_set_@prop@_from_@rel@(@obj@_id ref_id, @type@ val)" + block{
			o + "@rel@_set_@prop@(ref_id, val);";
		};
	} else if(ptype == property_type::other) {
	}

	return o;
}

basic_builder& make_join_getters_setters(basic_builder& o, relationship_object_def const& obj) {
	o + heading{ "convenience getters and setters that operate via an implcit join" };

	for(auto& inner_rprop : obj.properties) {
		for(auto& io : obj.indexed_objects) {
			if(obj.primary_key == io) {
				make_relation_unique_pk_join_getters_setters(o, inner_rprop.name, inner_rprop.data_type,
					io.type_name, obj.name, io.property_name, inner_rprop.type, obj.is_expandable);
			} else if(io.index == index_type::at_most_one) {
				make_relation_unique_non_pk_join_getters_setters(o, inner_rprop.name, inner_rprop.data_type,
					io.type_name, obj.name, io.property_name, inner_rprop.type, obj.is_expandable);
			} else if(io.index == index_type::many) {
				make_relation_many_join_getters_setters(o, inner_rprop.name,
					io.type_name, obj.name, io.property_name);
			}
		}
	}
	for(auto& inner_io : obj.indexed_objects) {
		for(auto& io : obj.indexed_objects) {
			if(&io != &inner_io) {
				if(obj.primary_key == io) {
					make_relation_unique_pk_join_getters_setters(o, inner_io.property_name, inner_io.type_name + "_id",
						io.type_name, obj.name, io.property_name, property_type::vectorizable, obj.is_expandable);
				} else if(io.index == index_type::at_most_one) {
					make_relation_unique_non_pk_join_getters_setters(o, inner_io.property_name, inner_io.type_name + "_id",
						io.type_name, obj.name, io.property_name, property_type::vectorizable, obj.is_expandable);
				} else if(io.index == index_type::many) {
					make_relation_many_join_getters_setters(o, inner_io.property_name,
						io.type_name, obj.name, io.property_name);
				}
			}
		}
	}

	o + line_break{};
	return o;
}
