#include <iostream>
#include <unordered_map>


#include <hilti/rt/libhilti.h>
#include <spicy/rt/libspicy.h>

#include <node_api.h>

#include <dlfcn.h>

// TODO:
// * Consider switching to C++ API (header-only), maybe.
// https://github.com/nodejs/node-addon-api/
//
// Addon examples
// https://github.com/nodejs/node-addon-examples
//
// clang-format !!!
//


#define dprintf(...) do { fprintf (stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)

namespace spicy::nodejs {


// Keep loaded hilti_modules from deconstructing (from spicy-plugin).
static std::unordered_map<std::string, hilti::rt::Library> _libraries;

// XXX: Attach this to some context or managed class.
static spicy::rt::Driver *spicy_rt_driver = nullptr;

// ".join([hex(ord(c))[2:] for c in "spicy-js-parser"])
static const napi_type_tag SpicyJsParserTag = {
  0x73706963792d6a73, 0x2d70617273657200,
};


#define NAPI_CALL(env, call) do {                                 \
	napi_status status = (call);                                  \
	if (status != napi_ok) {                                      \
	  const napi_extended_error_info* error_info = NULL;          \
      napi_get_last_error_info((env), &error_info);               \
      const char* err_message = error_info->error_message;        \
      bool is_pending;                                            \
      napi_is_exception_pending((env), &is_pending);              \
      if (!is_pending) {                                          \
        const char* message = (err_message == NULL)               \
            ? "empty error message"                               \
            : err_message;                                        \
        napi_throw_error((env), NULL, message);                   \
		return nullptr; \
	  } \
	} \
} while (0);

static napi_value napi_s(napi_env env, const std::string& s) {
	napi_value r;
	NAPI_CALL(env, napi_create_string_utf8(env, s.c_str(), s.size(), &r));
	return r;
}

// This is annoying
napi_value hilti_to_napi(napi_env env, const hilti::rt::type_info::Value& v, napi_value *result) {
    const auto& type = v.type();
	std::string spicy_id = type.id ? std::string(*type.id) : "";
	napi_value value = nullptr;
	std::string typestr;

	// If the value isn't set, return undefind.
	if (!v) {
		NAPI_CALL(env, napi_get_undefined(env, result));
		return nullptr;
	}
    switch ( type.tag ) {
		/*
        case TypeInfo::Undefined: throw std::runtime_error("unhandled type");
        case TypeInfo::Address: return type.address->get(v);
        case TypeInfo::Any: return "<any>";
		*/
        case hilti::rt::TypeInfo::Bool: {
			typestr = "Bool";
			NAPI_CALL(env, napi_get_boolean(env, type.bool_->get(v), &value));
			break;
		}
        case hilti::rt::TypeInfo::Bytes: {
			// Hmm, hmm. Spicy knows bytes and we translate them into a typed array of uint8.
			// It's up to script-land to interpret these bytes as strings or whatever they
			// represent, we don't know string semantics down here.
			typestr = "Bytes";
			char *array_buf = nullptr;
			auto bytes = type.bytes->get(v);
			napi_value arraybuffer;
			NAPI_CALL(env, napi_create_arraybuffer(env, bytes.size(), reinterpret_cast<void **>(&array_buf), &arraybuffer));
			std::memcpy(array_buf, bytes.data(), bytes.size());
			NAPI_CALL(env, napi_create_typedarray(env, napi_uint8_array, bytes.size(), arraybuffer, 0, &value));
			break;
		}
		/*
        case TypeInfo::BytesIterator: return to_string(type.bytes_iterator->get(v));
		*/
        case hilti::rt::TypeInfo::Enum: {
			typestr = "Enum";
			napi_create_string_utf8(env, type.enum_->get(v).name.c_str(), NAPI_AUTO_LENGTH, &value);
			break;
		}
		/*
        case TypeInfo::Error: return to_string(type.error->get(v));
        case TypeInfo::Exception: return to_string(type.exception->get(v));
        case TypeInfo::Function: return "<function>";
        case TypeInfo::Interval: return type.interval->get(v).seconds();
        case TypeInfo::Library: return "<library value>";
        case TypeInfo::Map: {
            auto j = json::array();

            for ( auto [key, value] : type.map->iterate(v) )
                j.push_back({convert(key), convert(value)});
            return j;
        }
        case TypeInfo::MapIterator: {
            auto [key, value] = type.map_iterator->value(v);
            return json::array({convert(key), convert(value)});
        }
        case TypeInfo::Network: {
            Network n = type.network->get(v);
            return json::object({{"prefix", n.prefix()}, {"length", n.length()}});
        }
        case TypeInfo::Optional: {
            auto y = type.optional->value(v);
            return y ? convert(y) : json();
        }
        case TypeInfo::Port: {
            Port p = type.port->get(v);
            return json::object({{"port", p.port()}, {"protocol", to_string(p.protocol())}});
        }
        case TypeInfo::Real: return type.real->get(v);
        case TypeInfo::RegExp: return to_string(type.regexp->get(v));
        case TypeInfo::Result: {
            auto y = type.result->value(v);
            return y ? convert(y) : json();
        }
        case TypeInfo::Set: {
            auto j = json::array();

            for ( auto i : type.set->iterate(v) )
                j.push_back(convert(i));

            return j;
        }
        case TypeInfo::SetIterator: return convert(type.set_iterator->value(v));
        case TypeInfo::SignedInteger_int8: return type.signed_integer_int8->get(v);
        case TypeInfo::SignedInteger_int16: return type.signed_integer_int16->get(v);
        case TypeInfo::SignedInteger_int32: return type.signed_integer_int32->get(v);
        case TypeInfo::SignedInteger_int64: return type.signed_integer_int64->get(v);
        case TypeInfo::Stream: return to_string_for_print(type.stream->get(v));
        case TypeInfo::StreamIterator: return to_string_for_print(type.stream_iterator->get(v));
        case TypeInfo::StreamView: return to_string_for_print(type.stream_view->get(v));
        case TypeInfo::String: return type.string->get(v);
        case TypeInfo::StrongReference: {
            auto y = type.strong_reference->value(v);
            return y ? convert(y) : json();
        }
		*/
        case hilti::rt::TypeInfo::Struct: {
			NAPI_CALL(env, napi_create_object(env, &value));
			typestr = "Struct";

            for ( const auto& [f, y] : type.struct_->iterate(v) ) {
				napi_value key;
				NAPI_CALL(env, napi_create_string_utf8(env, f.name.c_str(), NAPI_AUTO_LENGTH, &key));
				napi_value field_value;
				hilti_to_napi(env, y, &field_value);

				NAPI_CALL(env, napi_set_property(env, value, key, field_value));
            }
			break;
		}
		/*
        case TypeInfo::Time: return type.time->get(v).seconds();
		*/
        case hilti::rt::TypeInfo::Tuple: {
			typestr = "Tuple";
			napi_create_array_with_length(env, type.tuple->elements().size(), &value);

			int j = 0;
            for ( auto i : type.tuple->iterate(v) ) {
				napi_value element = nullptr;
				hilti_to_napi(env, i.second, &element);
                NAPI_CALL(env, napi_set_element(env, value, j, element));
				j++;
			}
			break;
        }
		/*
        case TypeInfo::Union: {
            auto y = type.union_->value(v);
            return y ? convert(y) : json();
        }
        case TypeInfo::UnsignedInteger_uint8: return type.unsigned_integer_uint8->get(v);
        case TypeInfo::UnsignedInteger_uint16: return type.unsigned_integer_uint16->get(v);
        case TypeInfo::UnsignedInteger_uint32: return type.unsigned_integer_uint32->get(v);
        case TypeInfo::UnsignedInteger_uint64: return type.unsigned_integer_uint64->get(v);
		*/
        case hilti::rt::TypeInfo::ValueReference: {
			typestr = "ValueReference";
            auto y = type.value_reference->value(v);
			return hilti_to_napi(env, y, result);
        }
        case hilti::rt::TypeInfo::Vector: {
			// TODO: It would be nice to pre-allocate the array, but I can't figure
			//       out how to get the length of v when it is a vector.
			typestr = "Vector";
			NAPI_CALL(env, napi_create_array(env, &value));

			int j = 0;

            for ( auto i : type.vector->iterate(v) ) {
				napi_value element = nullptr;
				hilti_to_napi(env, i, &element);
                NAPI_CALL(env, napi_set_element(env, value, j, element));
				j++;
			}
			break;
        }
		/*
        case TypeInfo::VectorIterator: return convert(type.vector_iterator->value(v));
        case TypeInfo::Void: return "<void>";
        case TypeInfo::WeakReference: {
            auto y = type.weak_reference->value(v);
            return y ? convert(y) : json();
        }
		*/
    }

	NAPI_CALL(env, napi_create_object(env, result));
	if (value) {
		NAPI_CALL(env, napi_set_property(env, *result, napi_s(env, "value"), value));
		NAPI_CALL(env, napi_set_property(env, *result, napi_s(env, "type"), napi_s(env, typestr)));
		if (!spicy_id.empty())
			NAPI_CALL(env, napi_set_property(env, *result, napi_s(env, "id"), napi_s(env, spicy_id)));
	} else {
		std::string err = std::string("Not implemented: ") + spicy_id + " " + type.display + " " + " " + std::to_string(type.tag);
		dprintf("hilti_to_napi failed: %s", err.c_str());
		NAPI_CALL(env, napi_get_null(env, result));
	}
	return nullptr;
}

static napi_value ProcessInput(napi_env env, napi_callback_info cbinfo) {
	napi_value argv[3];
	size_t argc = sizeof(argv) / sizeof(argv[0]);
	NAPI_CALL(env, napi_get_cb_info(env, cbinfo, &argc, argv, nullptr, nullptr));

	if (argc != 2) {
		napi_throw_error(env, "WrongArguments", "Expected two args");
		return nullptr;
	}

	bool is_spicy_parser = false;
	NAPI_CALL(env, napi_check_object_type_tag(env, argv[0], &SpicyJsParserTag, &is_spicy_parser));

	if (!is_spicy_parser) {
		napi_throw_type_error(env, "NoParser", "Not a spicy parser");
		return nullptr;
	}

	spicy::rt::Parser *parser;

	NAPI_CALL(env, napi_unwrap(env, argv[0], reinterpret_cast<void **>(&parser)));

	size_t data_len;
	NAPI_CALL(env, napi_get_value_string_utf8(env, argv[1], nullptr, 0, &data_len));

	char *data = new char[data_len + 1];  // Add the NULL byte that get_value_string_utf8 wants.
	NAPI_CALL(env, napi_get_value_string_utf8(env, argv[1], data, data_len + 1, nullptr));

	// Treat as a binary string
	std::istringstream is(std::string(data, data_len));

	hilti::rt::Result<spicy::rt::ParsedUnit> result;
	try {
		result = spicy_rt_driver->processInput(*parser, is);

		if (!result) {
			std::cerr << "ERROR " << result.error() << std::endl;
			napi_throw_error(env, "ParseError", result.error().description().c_str());
		}
	} catch (spicy::rt::ParseError &err) {
			std::cerr << "Caught error" << std::endl;
			napi_throw_error(env, "ParseError", err.what());
	}

	delete[] data;

	napi_value nresult;
	hilti_to_napi(env, (*result).value(), &nresult);
	return nresult;
}

static napi_value Parsers(napi_env env, napi_callback_info cbinfo) {
	auto parsers = spicy::rt::parsers();

	napi_value array;
	NAPI_CALL(env, napi_create_array_with_length(env, parsers.size(), &array));

	int i = 0;

	napi_value name_key = napi_s(env, "name");

	for (auto const p: parsers) {
		napi_value po;
		NAPI_CALL(env, napi_create_object(env, &po));

		NAPI_CALL(env, napi_type_tag_object(env, po, &SpicyJsParserTag));
		NAPI_CALL(env, napi_wrap(env, po, (void *)p, nullptr, nullptr, nullptr));

		NAPI_CALL(env, napi_set_property(env, po, name_key, napi_s(env, p->name)));
		NAPI_CALL(env, napi_set_element(env, array, i, po));

		i++;
	}
	return array;
}



static napi_value Init(napi_env env, napi_callback_info cbinfo) {
	try {
		hilti::rt::init();
		spicy::rt::init();
	} catch ( const hilti::rt::Exception& e ) {
		napi_throw_error(env, "InitError", e.what());
		return nullptr;
	}

	spicy_rt_driver = new spicy::rt::Driver();
	return nullptr;
}


// Load .hlto binary
static napi_value Load(napi_env env, napi_callback_info cbinfo) {
	napi_value argv[2];
	size_t argc = sizeof(argv) / sizeof(argv[0]);
	NAPI_CALL(env, napi_get_cb_info(env, cbinfo, &argc, argv, nullptr, nullptr));
	if (argc != 1) {
		napi_throw_error(env, "WrongArguments", "Expected single argument");
		return nullptr;
	}

	char fn[PATH_MAX + 1];
	size_t fn_len;
	NAPI_CALL(env, napi_get_value_string_utf8(env, argv[0], fn, sizeof(fn), &fn_len));

	hilti::rt::filesystem::path p(fn);
	std::string key = p.lexically_normal().string();

	try {
		if (auto [library, inserted] = _libraries.insert({key, hilti::rt::Library(fn)}); inserted) {

			auto loaded = library->second.open();
			if (!loaded) {
				std::string err = "failed to load: ";
				err += *fn;
				err += ": " + std::string(loaded.error());
				napi_throw_error(env, "ParserLoadError", err.c_str());
			}
		}
	} catch (const hilti::rt::filesystem::filesystem_error& e) {
		napi_throw_error(env, "ParserLoadError", e.what());
	}

	return nullptr;
}

static napi_value Version(napi_env env, napi_callback_info cb_info) {
	napi_value obj;
	NAPI_CALL(env, napi_create_object(env, &obj));
	napi_value spicy_key = napi_s(env, "spicy");
	napi_value hilti_key = napi_s(env, "hilti");
	napi_value spicy_version = napi_s(env, spicy::rt::version());
	napi_value hilti_version = napi_s(env, hilti::rt::version());

	NAPI_CALL(env, napi_set_property(env, obj, spicy_key, spicy_version));
	NAPI_CALL(env, napi_set_property(env, obj, hilti_key, hilti_version));

	return obj;
}

static napi_value __initialize(napi_env env, napi_callback_info cbinfo) {
	napi_value argv[3];
	size_t argc = sizeof(argv) / sizeof(argv[0]);
	NAPI_CALL(env, napi_get_cb_info(env, cbinfo, &argc, argv, nullptr, nullptr));
	if (argc != 2) {
		napi_throw_error(env, "WrongArguments", "Expected two args");
		return nullptr;
	}

	// Hmm, hmm: This is available with define NAPI_EXPERIMENTAL so we would not
	// need to play tricks with passing the addon_path into the __initialize:
	//
	//     node_api_get_module_file_name
	//

	char addon_path[PATH_MAX + 1];
	size_t addon_path_len;
	NAPI_CALL(env, napi_get_value_string_utf8(env, argv[1], addon_path, sizeof(addon_path), &addon_path_len));

	// Hack: Re-open the addon file with RTLD_GLOBAL so that
	// .hlto files have access to the symbols from the -rt libs
	// linked into this one.
	// Without this, loading .hlto fails with errors like:
	//
	// Failed to load library "my-http.hlto": my-http.hlto: undefined symbol: _ZN5hilti2rt12InvalidValueD1Ev'
	//
	// http://missioncriticallabs.com/blog/dependencies-between-c-addons-in-node
	//
	// This may only be needed on Linux...
	void *handle = dlopen(addon_path, RTLD_NOW | RTLD_GLOBAL);
	if (!handle) {
		std::cerr << "ERROR: Failed to dlopen " << addon_path << " with RTLD_GLOBAL" << std::endl;
		napi_throw_error(env, "Error", "Failed to dlopen");
		return nullptr;
	}

	#define EXPORT_METHOD(name, method) \
		{#name, NULL, method, NULL, NULL, NULL, (napi_property_attributes)(napi_writable | napi_enumerable | napi_configurable), NULL}

	napi_property_descriptor properties[] = {
		EXPORT_METHOD(version, Version),
		EXPORT_METHOD(load, Load),
		EXPORT_METHOD(init, Init),
		EXPORT_METHOD(parsers, Parsers),
		EXPORT_METHOD(processInput, ProcessInput),
	};
	NAPI_CALL(env, napi_define_properties(env, argv[0], sizeof(properties) / sizeof(properties[0]), properties));

	return nullptr;
}

static napi_value AddonInit(napi_env env, napi_value exports) {
	napi_property_descriptor properties[] = {
		EXPORT_METHOD(__initialize, __initialize),
	};
	NAPI_CALL(env, napi_define_properties(env, exports, sizeof(properties) / sizeof(properties[0]), properties));

	return nullptr;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, AddonInit);

} // spicy::nodejs