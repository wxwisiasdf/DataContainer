# Data container generator input file specification

## Overview

This document contains the full description of the files that can be processed by the data container generator and turned into a C++ header file.

## File name

There are no limitations on the name of the file. If the name of the file ends with a `.` followed by three characters, the generated C++ header file will have the same name except with those trailing characters replaced by `hpp`. For example, `file_name.txt` would generate the file `file_name.hpp`. If the name of the file does not end in that way, the generated file will have the same name except with `.hpp` appended to it. For example, `file_name_b` would generate the file `file_name_b.hpp`.

## Keys and parameters

The contents of the file are composed of a sequence of keys with parameters. A key is simply a series of non-whitespace characters that contains neither `{` not `}`. (Whitespace characters in the context of this document mean the following four values: the ASCII newline `\n`, the ASCII tab `\t`, the ASCII space, and the ASCII carriage return `\r`). A key can be followed by zero or more whitespace characters and then zero or more parameters, which are arbitrary sequences of characters enclosed by `{` and `}`. The contents of a parameter may include nested nested keys with parameters. When being processed any whitespace characters following the initial `{` prior to a a non-whitespace character will be ignored, as will any whitespace characters preceding the final `}`. 

### Examples

- `value_a value_b` is two keys, `value_a` and `value_b`, neither with any parameters
- `key_value { parameter value }` is a key with `key_value` as its key value and a single parameter containing the value `parameter value`
- `key_value{p1}{p2}` is a key with `key_value` as its key value and two parameters, containing `p1` and `p2` as their values
- `outer_key{ innter_key{ ival } }` is a key with `outer_key` as its key value and a single parameter containing the value `innter_key{ ival }`. If recursive parsing of this parameter is called for, it will turn into the expected key with a single parameter containing `ival`.

## Top level keys

As already mentioned, the data container generator processes the file as a sequence of keys. Only the keys described in this section may occur at the top level of the file (that is, not contained within a parameter of some key).

### `namespace`

The `namespace` key expects a single parameter. This parameter determines the name of the namespace that the contents of the generated file are placed within (if no `namespace` key is encountered, the default is `dcon`). For example, `namespace{my_namespace}` will result in the contents of the generated file being placed within the `my_namespace` namespace. This is necessary if you wish to include the results of two different generated files within the same project, as otherwise they will generate classes and function with the same names.

### `include`

The `include` key expects a single parameter. Each occurrence of this key will result in an additional `#include ...` directive at the beginning of the generated file with `...` replaced by its parameter, which may be necessary if any properties have user-defined data types. `include{"my_file.hpp"}` will result in `#include "my_file.hpp"`, while `include{<vector>}` will result in `#include <vector>`.

### `global`

The `global` key expects a single parameter. Each occurrence of this key will result in an additional line at the end of the generated `data_container` class, with the verbatim contents of the `global` parameter. This may be necessary if you want to access any outside information from within a user-defined hook function or conversion routine, since as member functions of `data_container` they can see any member variables added by a `global` key. For example, `global{my_data_type* global_pointer = nullptr;}` will result in the generated `data_container` having an additional public member variable named `global_pointer` of type `my_data_type*`;

### `load_save`

The `load_save` key expects a single parameter that will in turn be processed as a sequence of sub-keys with their own parameters. These sub-keys must be either one of the following four options: `only_objects`, `exclude_objects`, `only_properties`, and `exclude_properties`, each of which may come with one or more parameters, or be `name` with a single parameter. These sub-keys collectively define the name of a serialization record and the objects and properties that it will write out. (See [serialization](serialization.md) for more details.)

For example:

```
load_save{
	name{store_standard}
	only_objects{standard}{bonus}
	exclude_properties{discard}
}
```

will result in a `make_serialize_record_store_standard` function for producing a serialization record that, when used, will serialize the objects with a `standard` or `bonus` tag by serializing all their properties except those marked with a `discard` tag.

### `convert`

The `convert` key expects a single parameter that will in turn be processed as a sequence of sub-keys with their own parameters. These sub-keys must be either `to` with a single parameter or the key `from` with a single parameter. Each `convert` key will result in a member function `to_type convert_type(from_type const* source, to_type* overload_selector) const;` member function, with `to_type` and `from_type` replaced by the parameters of the `to` and `from` keys respectively, that you must provide an external implementation for. These conversion routines are used as part of deserialization to convert properties serialized in a previous version as some user-defined type to the user-defined type that they are currently defined as having. (See [serialization](serialization.md) for more details.)

For example:

```
convert{
	from{my_type}
	to{my_type_v2}
}
```

will result in conversion routine `my_type_v2 convert_type(my_type const* source, my_type_v2* overload_selector) const;` for converting properties serialized as values of `my_type` into values of type `my_type_v2`.

### `legacy_types`

The `legacy_types` key expects a single parameter that will in turn be processed as a sequence of sub-keys with their own parameters. Each sub-key must be `name` with a single parameter. Each parameter for these sub-keys represents a type that requires a user-defined deserialize routine, but which is no longer actually defined as the object type for any property (i.e. some previous version may have serialized properties defined as objects of this type, but the current version no longer uses object of this type). Only the appropriate `void deserialize(std::byte const*& input_buffer, type_name& obj, std::byte const* end) const;` will require a user-provided definition, and *not* the `serialize_size` and `serialize` functions.

For example:

```
legacy_types{
	name{my_type}
	name{my_type_v2}
}
```

defines `my_type` and `my_type_v2` as old types that may need special deserialization, but which are no longer used.

### `object`

The `object` key expects a single parameter that will in turn be processed as a sequence of sub-keys with their own parameters. The valid sub-keys are as follows:

- `name{...}` with a single parameter. This defines the name of the object.
- `storage_type{...}` with a single parameter. The parameter must be one of `contiguous`, `erasable`, or `compactable`. This defines how the object's properties are stored.
- `size{...}` with a single parameter. This parameter must be either a numerical value or `expandable`. If it is a numerical value it defines the maximum number of values that may be stored for this type of object, while `expandable` means that each property will be stored in a `std::vector` with its size increased as needed.
- `tag{...}` with a single parameter. These tags are referenced by `load_save` to determine which objects will have their properties serialized.
- `hook{...}` with a single parameter. The parameter must be one of `create`, `delete`, or `move`. This will result in the corresponding `void on_create_ ...`, `void on_delete_ ...`, or `void on_move_ ...` declarations for user-provided definition that will be called upon a corresponding event for this object. Multiple `hook` keys may appear in a single `object` to hook multiple of these functions.
- `property{...}` with a single parameter. Each `property` key defines in its parameter a distinct property for the object. The contents of a `property` key parameter are defined in their own section below.

For example:

```
object{
	name{person}
	storage_type{contiguous}
	size{10000}
	
	property{
		name{age}
		type{int32_t}
	}
	property{
		name{is_alive}
		type{bitfield}
	}
}
```

### `relationship`

The `relationship` key expects a single parameter that will in turn be processed as a sequence of sub-keys with their own parameters. Any sub-key that is a valid as part of an `object` parameter may appear as a sub-key. Additionally, the following keys are also valid:

- `primary_key{...}` with a single parameter. This forces the link named in the parameter to be used as the primary key (it still requires a `link` definition, as described below). If no `primary_key` is defined, one will be chosen automatically if possible.
- `link{...}` described below
- `composite_key{...}` described below

For example:

```
relationship{
	name{car_ownership}
	tag{standard}
	
	link{
		object{person}
		name{owner}
		type{many}
		index_storage{std_vector}
	}
	link{
		object{car}
		name{owned_car}
		type{unique}
	}
	
	property{
		name{ownership_date}
		type{int32_t}
	}
}
```

## Complex sub-keys

### `property`

The `property` key expects a single parameter that will in turn be processed as a sequence of sub-keys with their own parameters. The valid sub-keys are as follows:

- `name{...}` with a single parameter. This defines the name of the property.
- `type{...}` with a single parameter. This parameter must itself by a key with zero or more parameters, which will determine the type of value stored in this property. Valid sub-keys are: `bitfield` with no parameters (for boolean values to be stored a packed bits), `derived{...}` with a single parameter (the single parameter defines the type of the value, but no storage will be allocated and it will require a hooked setter and/or getter to be usable), `vector_pool{...}{...}` with two parameters (the first a number determining the size of the memory pool backing the storage of the vectors, and the second the type of values stored in the vectors), `object{...}` with one parameter (which results in a property that stores value of the type named in the parameter and which are assumed to be *not* safe to `memcpy` and hence must have their constructors and destructors run and must be serialized and deserialized by custom routines), and finally `other` with no parameters, where `other` is not one of the keys listed above (which will result in a property that stores values of type `other`).
- `hook{...}` with a single parameter. The parameter must be either `get`, or `set`. This will result in the `... _get_ ...` or `... _set_ ...` function to require a user-provided definition. Multiple `hook` keys may appear in a single `property` to hook both get and set functions.
- `tag{...}` with a single parameter. These tags are referenced by `load_save` to determine which properties in an object will be serialized.

### `link`

The `link` key expects a single parameter that will in turn be processed as a sequence of sub-keys with their own parameters. Each `link` defines one object involved in the relationship, and a valid relationship contains two or more links. The valid sub-keys are as follows:

- `name{...}` with a single parameter. This defines the name of the link.
- `object{...}` with a single parameter. This defines the object connected by this link.
- `type{...}` with a single parameter. The parameter must be one of `unique`, `many`, or `unindexed`. 
- `index_storage{...}` with a single parameter. The parameter must be one of `list`, `array`, or `std_vector`. This is only valid for links with type `many` and defines how the index from individual objects to the individual relationship(s) they are linked to will be stored.

For example:

```
link{
	object{cat}
	name{owner}
	type{unique}
	index_storage{array}
}
```

### `composite_key`

The `composite_key` key expects a single parameter that will in turn be processed as a sequence of sub-keys with their own parameters. Each `composite_key` defines a composite key for the relationship that is the combination of two or more of the keys for linked objects. The valid sub-keys are as follows:

- `name{...}` with a single parameter. This defines the name of the composite key.
- `index{...}` with a single parameter. This defines one of the indexes that are part of the composite key. At least two `index` keys appear in a valid composite key.

For example:

```
composite_key{
	name{owner_cat_key}
	index{owner}
	index{owned_cat}
}
```

## Examples

## Errors

If any errors are detected while parsing an input file, the data container generator will produce a description of those errors as the contents of the output file (instead of the expected valid C++). Error detection in the parser is currently focused on detecting syntactic errors. Logical errors may pass unnoticed, resulting in C++ output that fails to compile. In the long run it would be nice to detect all of the logic errors as well, and if an undetected error has made life difficult for you, please let me know so that detecting it in the future can be given priority.