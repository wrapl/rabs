#ifndef MINILANG_H
#define MINILANG_H

#include "sha256.h"

typedef struct ml_t ml_t;
typedef struct ml_type_t ml_type_t;
typedef struct ml_value_t ml_value_t;

typedef ml_value_t *(*ml_callback_t)(ml_t *ML, void *Data, int Count, ml_value_t **Args);

typedef ml_value_t *(*ml_getter_t)(ml_t *ML, void *Data, const char *Name);
typedef ml_value_t *(*ml_setter_t)(ml_t *ML, void *Data, const char *Name, ml_value_t *Value);

ml_t *ml_new(void *Data, ml_getter_t Get);
void *ml_data(ml_t *ML);

ml_type_t *ml_class(const char *Name, ...);

ml_value_t *ml_load(ml_t *ML, const char *FileName);
ml_value_t *ml_call(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args);
ml_value_t *ml_index(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args);

void ml_method_add(const char *Method, void *Data, ml_callback_t Function, ...);

ml_value_t *ml_string(const char *Value, int Length);
ml_value_t *ml_integer(long Value);
ml_value_t *ml_real(double Value);
ml_value_t *ml_list();
ml_value_t *ml_tree();
ml_value_t *ml_function(void *Data, ml_callback_t Function);
ml_value_t *ml_object(void *Data, ml_type_t *Type);
ml_value_t *ml_property(void *Data, const char *Name, ml_getter_t Get, ml_setter_t Set, ml_getter_t Next);
ml_value_t *ml_error(const char *Error, const char *Format, ...);
ml_value_t *ml_reference(ml_value_t **Address);
ml_value_t *ml_method(const char *Name);

int ml_is_integer(ml_value_t *Value);
int ml_is_real(ml_value_t *Value);
int ml_is_string(ml_value_t *Value);
int ml_is_error(ml_value_t *Value);
int ml_is_list(ml_value_t *Value);
int ml_is_tree(ml_value_t *Value);
int ml_is_closure(ml_value_t *Value);

long ml_integer_value(ml_value_t *Value);
double ml_real_value(ml_value_t *Value);
const char *ml_string_value(ml_value_t *Value);
int ml_string_length(ml_value_t *Value);
const char *ml_error_type(ml_value_t *Value);
const char *ml_error_message(ml_value_t *Value);
void ml_closure_hash(ml_value_t *Closure, unsigned char Hash[SHA256_BLOCK_SIZE]);

int ml_list_foreach(ml_value_t *List, void *Data, int (*callback)(ml_value_t *, void *));
int ml_tree_foreach(ml_value_t *Tree, void *Data, int (*callback)(ml_value_t *, ml_value_t *, void *));

struct ml_value_t {
	const ml_type_t *Type;
};

extern ml_value_t Nil[];

#endif
