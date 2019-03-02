#include "library.h"
#include "rabs.h"
#ifdef __MINGW32__
#else
#include <dlfcn.h>
#endif
#include <gc.h>

static ml_type_t *LibraryT;

typedef struct library_t {
	const ml_type_t *Type;
	void *Handle, *Data;
} library_t;

ml_value_t *library_load(const char *Path, stringmap_t *Globals) {
#ifdef __MINGW32__
	return ml_error("PlatformError", "Dynamic libraries not supported");
#else
	void *Handle = dlopen(Path, RTLD_NOW);
	if (Handle) {
		void *(*init)(stringmap_t *) = dlsym(Handle, "init");
		library_t *Library = new(library_t);
		Library->Type = LibraryT;
		Library->Handle = Handle;
		if (init) Library->Data = init(Globals);
		return (ml_value_t *)Library;
	} else {
		return ml_error("LibraryError", "Failed to load %s: %s", Path, dlerror());
	}
#endif
}

static ml_value_t *library_get(void *Data, int Count, ml_value_t **Args) {
#ifdef __MINGW32__
	return ml_error("PlatformError", "Dynamic libraries not supported");
#else
	library_t *Library = (library_t *)Args[0];
	const char *Symbol = ml_string_value(Args[1]);
	ml_callback_t Callback = dlsym(Library->Handle, Symbol);
	return ml_function(Library->Data, Callback);
#endif
}

void library_init() {
	LibraryT = ml_class(MLAnyT, "library");
	ml_method_by_name(".", 0, library_get, LibraryT, MLStringT, NULL);
}
