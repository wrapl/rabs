#include "library.h"
#include "rabs.h"
#ifdef Mingw
#else
#include <dlfcn.h>
#endif
#include <gc/gc.h>

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

static void library_get(ml_state_t *Caller, library_t *Library, int Count, ml_value_t **Args) {
	ML_CHECKX_ARG_COUNT(1);
	ML_CHECKX_ARG_TYPE(0, MLStringT);
	const char *Symbol = ml_string_value(Args[0]);
	ml_value_t *Value = stringmap_search(Library->Exports, Symbol);
	ML_CONTINUE(Caller, Value ?: ml_error("LibraryError", "Symbol %s not exported from %s", Symbol, Library->Path));
}

void library_init() {
	LibraryT = ml_type(MLAnyT, "library");
	LibraryT->call = (void *)library_get;
}
