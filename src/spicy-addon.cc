#include <iostream>
#include <unordered_map>


#include <hilti/rt/libhilti.h>
#include <spicy/rt/libspicy.h>

#include <node.h>

#include <dlfcn.h>


namespace spicy::nodejs {


// Keep loaded hilti_modules from deconstructing (from spicy-plugin).
static std::unordered_map<std::string, hilti::rt::Library> _libraries;

// XXX: Attach this to some context or managed class.
static spicy::rt::Driver *spicy_rt_driver = nullptr;

static v8::Local<v8::String> v8_str(v8::Isolate *isolate, const char *s) {
	return v8::String::NewFromUtf8(isolate, s).ToLocalChecked();
}
static v8::Local<v8::String> v8_str(v8::Isolate *isolate, const std::string& s) {
	return v8_str(isolate, s.c_str());
}

void Version(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate *isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();

	v8::Local<v8::Object> obj = v8::Object::New(isolate);
	obj->Set(context, v8_str(isolate, "spicy"), v8_str(isolate, spicy::rt::version().c_str())).Check();
	obj->Set(context, v8_str(isolate, "hilti"), v8_str(isolate, hilti::rt::version().c_str())).Check();

	args.GetReturnValue().Set(obj);
}

void Parsers(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate *isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();

	auto parsers = spicy::rt::parsers();
	// std::cout << "Got " << parsers.size() << " parsers " << std::endl;

	auto array = v8::Array::New(isolate, parsers.size());

	int i = 0;

	for (auto const p: parsers) {
		// std::cout << p->name << std::endl;
		v8::Local<v8::Object> obj = v8::Object::New(isolate);
		obj->Set(context, v8_str(isolate, "name"), v8_str(isolate, p->name.c_str())).Check();
		array->Set(context, i, obj).Check();
	}

	args.GetReturnValue().Set(array);




}


/* Load .hlto binary */
void Load(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate *isolate = args.GetIsolate();
	if (args.Length() != 1) {
		isolate->ThrowException(v8::Exception::Error(v8_str(isolate, "expected single string argument")));
		return;
	}
	auto fn = v8::String::Utf8Value(isolate, args[0]);
	hilti::rt::filesystem::path p(*fn);
	std::string key = p.lexically_normal().string();

	try {
		if (auto [library, inserted] = _libraries.insert({key, hilti::rt::Library(*fn)}); inserted) {

			auto loaded = library->second.open();
			if (!loaded) {
				std::string err = "failed to load: ";
				err += *fn;
				err += ": " + std::string(loaded.error());
				isolate->ThrowException(v8::Exception::Error(v8_str(isolate, err.c_str())));
				return;
			}
		}
	} catch (const hilti::rt::filesystem::filesystem_error& e) {
		isolate->ThrowException(v8::Exception::Error(v8_str(isolate, e.what())));
	}
}


void Init(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate *isolate = args.GetIsolate();
	try {
		hilti::rt::init();
		spicy::rt::init();
	} catch ( const hilti::rt::Exception& e ) {
		isolate->ThrowException(v8::Exception::Error(v8_str(isolate, e.what())));
		return;
	}

	spicy_rt_driver = new spicy::rt::Driver();
}

// I wonder if we need this. Maybe parsers() and doing it in Javascript
// is good enough.
void LookupParser(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate *isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();

	if (!spicy_rt_driver) {
		isolate->ThrowException(v8::Exception::Error(v8_str(isolate, "spicy.init() was not called")));
	}

	if (args.Length() != 1) {
		isolate->ThrowException(v8::Exception::Error(v8_str(isolate, "expected single string argument")));
		return;
	}
	auto parser_name = v8::String::Utf8Value(isolate, args[0]);

	auto result = spicy_rt_driver->lookupParser(*parser_name);
	if (!result) {
		isolate->ThrowException(v8::Exception::Error(v8_str(isolate, result.error().description())));
		return;
	}

	const spicy::rt::Parser *parser = result.value();

	v8::Local<v8::Object> obj = v8::Object::New(isolate);
	obj->Set(context, v8_str(isolate, "name"), v8_str(isolate, parser->name.c_str())).Check();

	args.GetReturnValue().Set(obj);
}


void Initialize(v8::Local<v8::Object> exports, v8::Local<v8::Value> mod, void *priv) {

	// XXX: Either we do auto-discovery of .hlto files at require()
	//      time (implement it in C++), or we ask the user to follow
	//      a protocol where they call load() for every .hlto file
	//      explicitly, followed by a call to init./
	auto isolate = exports->GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();

	auto mod_obj = v8::Local<v8::Object>::Cast(mod);

	v8::Local<v8::Value> fn = mod_obj->Get(context, v8_str(isolate, "filename")).ToLocalChecked();
	v8::String::Utf8Value fn_str(isolate, fn);

	// Hack: Re-open the add-on binary with RTLD_GLOBAL so that
	// .hlto files have access to the symbols linked into it.
	// Without this, loading .hlto fails with errors like:
	//
	// failed to load library "my-http.hlto": my-http.hlto: undefined symbol: _ZN5hilti2rt12InvalidValueD1Ev'
	//
	// http://missioncriticallabs.com/blog/dependencies-between-c-addons-in-node
	void *handle = dlopen(*fn_str, RTLD_NOW | RTLD_GLOBAL);
	if (!handle) {
		std::cerr << "ERROR: Failed to dlopen " << *fn_str << " with RTLD_GLOBAL" << std::endl;
		return;
	}

	NODE_SET_METHOD(exports, "load", Load);
	NODE_SET_METHOD(exports, "init", Init);
	NODE_SET_METHOD(exports, "lookupParser", LookupParser);
	NODE_SET_METHOD(exports, "version", Version);
	NODE_SET_METHOD(exports, "parsers", Parsers);
}


NODE_MODULE(NODE_GYP_MODULE_NAME, Initialize)

};
