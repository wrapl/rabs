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
	const char *Path;
	stringmap_t Exports[1];
} library_t;

ml_value_t *library_load(const char *Path, stringmap_t *Globals) {
#if defined(Linux)
	void *Handle = dlopen(Path, RTLD_NOW);
	if (Handle) {
		int (*init)(stringmap_t *, stringmap_t *) = dlsym(Handle, "init");
		if (!init) {
			dlclose(Handle);
			ml_error("LibraryError", "init function missing from %s", Path);
		}
		library_t *Library = new(library_t);
		Library->Type = LibraryT;
		Library->Path = GC_strdup(Path);
		init(Library->Exports, Globals);
		return (ml_value_t *)Library;
	} else {
		return ml_error("LibraryError", "Failed to load %s: %s", Path, dlerror());
	}
#else
	return ml_error("PlatformError", "Dynamic libraries not supported");
#endif
}

static ml_value_t *library_get(void *Data, int Count, ml_value_t **Args) {
	library_t *Library = (library_t *)Args[0];
	const char *Symbol = ml_string_value(Args[1]);
	ml_value_t *Value = stringmap_search(Library->Exports, Symbol);
	if (!Value) return ml_error("LibraryError", "Symbol %s not exported from %s", Symbol, Library->Path);
	return Value;
}

void library_init() {
	LibraryT = ml_type(MLAnyT, "library");
	ml_method_by_name(".", 0, library_get, LibraryT, MLStringT, NULL);
}
