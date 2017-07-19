#define _GNU_SOURCE

#include "minilang.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <gc.h>
#include <setjmp.h>
#include <ctype.h>

#define MAX_TRACE 16
#define new(T) ((T *)GC_MALLOC(sizeof(T)))
#define anew(T, N) ((T *)GC_MALLOC((N) * sizeof(T)))
#define snew(N) ((char *)GC_MALLOC_ATOMIC(N))
#define xnew(T, N, U) ((T *)GC_MALLOC(sizeof(T) + (N) * sizeof(U)))

typedef struct ml_reference_t ml_reference_t;
typedef struct ml_integer_t ml_integer_t;
typedef struct ml_real_t ml_real_t;
typedef struct ml_string_t ml_string_t;
typedef struct ml_list_t ml_list_t;
typedef struct ml_tree_t ml_tree_t;
typedef struct ml_object_t ml_object_t;
typedef struct ml_property_t ml_property_t;
typedef struct ml_closure_t ml_closure_t;
typedef struct ml_function_t ml_function_t;
typedef struct ml_method_t ml_method_t;
typedef struct ml_error_t ml_error_t;

typedef struct ml_frame_t ml_frame_t;
typedef struct ml_inst_t ml_inst_t;

typedef struct ml_list_node_t ml_list_node_t;
typedef struct ml_tree_node_t ml_tree_node_t;

typedef struct ml_source_t ml_source_t;

struct ml_source_t {
	const char *Name;
	int Line;
};

struct ml_t {
	void *Data;
	ml_getter_t Get;
	ml_value_t *Error;
	jmp_buf OnError;
};

long ml_default_hash(ml_t *ML, ml_value_t *Value) {
	return (long)(ptrdiff_t)Value->Type;
}

ml_value_t *ml_default_call(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	return ml_error("TypeError", "value is not callable");
}

ml_value_t *ml_default_index(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	return ml_error("TypeError", "value is not indexable");
}

ml_value_t *ml_default_deref(ml_t *ML, ml_value_t *Ref) {
	return Ref;
}

ml_value_t *ml_default_assign(ml_t *ML, ml_value_t *Ref, ml_value_t *Value) {
	return ml_error("TypeError", "value is not assignable");
}

ml_value_t *ml_default_next(ml_t *ML, ml_value_t *Iter) {
	return ml_error("TypeError", "value is not iterable");
}

ml_value_t *ml_default_key(ml_t *ML, ml_value_t *Iter) {
	return Nil;
}

ml_value_t *CompareMethod;

ml_type_t AnyT[1] = {{
	0,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

static ml_value_t *ml_nil_to_string(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	return ml_string("nil", 3);
}

ml_type_t NilT[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t Nil[1] = {{NilT}};

long ml_hash(ml_t *ML, ml_value_t *Value) {
	Value = Value->Type->deref(ML, Value);
	return Value->Type->hash(ML, Value);
}

ml_value_t *ml_call(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	Value = Value->Type->deref(ML, Value);
	for (int I = 0; I < Count; ++I) Args[I] = Args[I]->Type->deref(ML, Args[I]);
	return Value->Type->call(ML, Value, Count, Args);
}

ml_value_t *ml_index(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	Value = Value->Type->deref(ML, Value);
	for (int I = 0; I < Count; ++I) Args[I] = Args[I]->Type->deref(ML, Args[I]);
	return Value->Type->index(ML, Value, Count, Args);
}

ml_value_t *ml_inline(ml_t *ML, ml_value_t *Value, int Count, ...) {
	ml_value_t *Args[Count];
	va_list List;
	va_start(List, Count);
	for (int I = 0; I < Count; ++I) Args[I] = va_arg(List, ml_value_t *);
	va_end(List);
	return ml_call(ML, Value, Count, Args);
}

static long ml_function_hash(ml_t *ML, ml_value_t *Value) {
	ml_function_t *Function = (ml_function_t *)Value;
	return (long)(ptrdiff_t)Function->Callback;
}

static ml_value_t *ml_function_call(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	ml_function_t *Function = (ml_function_t *)Value;
	return (Function->Callback)(ML, Function->Data, Count, Args);
}

ml_type_t FunctionT[1] = {{
	AnyT,
	ml_function_hash,
	ml_function_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t *ml_function(void *Data, ml_callback_t Callback) {
	ml_function_t *Function = new(ml_function_t);
	Function->Type = FunctionT;
	Function->Callback = Callback;
	return (ml_value_t *)Function;
}

struct ml_integer_t {
	const ml_type_t *Type;
	long Value;
};

struct ml_real_t {
	const ml_type_t *Type;
	double Value;
};

struct ml_string_t {
	const ml_type_t *Type;
	const char *Value;
	int Length;
};

static long ml_integer_hash(ml_t *ML, ml_value_t *Value) {
	ml_integer_t *Integer = (ml_integer_t *)Value;
	return Integer->Value;
}

ml_type_t IntegerT[1] = {{
	AnyT,
	ml_integer_hash,
	ml_default_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t *ml_integer(long Value) {
	ml_integer_t *Integer = new(ml_integer_t);
	Integer->Type = IntegerT;
	Integer->Value = Value;
	return (ml_value_t *)Integer;
}

int ml_is_integer(ml_value_t *Value) {
	return Value->Type == IntegerT;
}

long ml_integer_value(ml_value_t *Value) {
	return ((ml_integer_t *)Value)->Value;
}

static long ml_real_hash(ml_t *ML, ml_value_t *Value) {
	ml_real_t *Real = (ml_real_t *)Value;
	return (long)Real->Value;
}

ml_type_t RealT[1] = {{
	AnyT,
	ml_real_hash,
	ml_default_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t *ml_real(double Value) {
	ml_real_t *Real = new(ml_real_t);
	Real->Type = RealT;
	Real->Value = Value;
	return (ml_value_t *)Real;
}

int ml_is_real(ml_value_t *Value) {
	return Value->Type == RealT;
}

double ml_real_value(ml_value_t *Value) {
	return ((ml_real_t *)Value)->Value;
}

static long ml_string_hash(ml_t *ML, ml_value_t *Value) {
	ml_string_t *String = (ml_string_t *)Value;
	long Hash = 5381;
	for (int I = 0; I < String->Length; ++I) Hash = ((Hash << 5) + Hash) + String->Value[I];
	return Hash;
}

const char *ml_string_value(ml_value_t *Value) {
	return ((ml_string_t *)Value)->Value;
}

int ml_string_length(ml_value_t *Value) {
	return ((ml_string_t *)Value)->Length;
}

ml_type_t StringT[1] = {{
	AnyT,
	ml_string_hash,
	ml_default_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t *ml_string(const char *Value, int Length) {
	ml_string_t *String = new(ml_string_t);
	String->Type = StringT;
	String->Value = Value;
	String->Length = Length >= 0 ? Length : strlen(Value);
	return (ml_value_t *)String;
}

int ml_is_string(ml_value_t *Value) {
	return Value->Type == StringT;
}

static ml_value_t *ml_string_new(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	static ml_value_t *StringMethod = 0;
	if (!StringMethod) StringMethod = ml_method("string");
	const char *Values[Count];
	int Lengths[Count];
	int Length = 0;
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Arg = Args[I];
		if (Arg->Type != StringT) {
			Arg = ml_call(ML, StringMethod, 1, &Arg);
			if (Arg->Type == ErrorT) return Arg;
			if (Arg->Type != StringT) return ml_error("ResultError", "string method did not return string");
		}
		Values[I] = ((ml_string_t *)Arg)->Value;
		Lengths[I] = ((ml_string_t *)Arg)->Length;
		Length += ((ml_string_t *)Arg)->Length;
	}
	char *Value = snew(Length + 1);
	char *End = Value;
	for (int I = 0; I < Count; ++I) {
		memcpy(End, Values[I], Lengths[I]);
		End += Lengths[I];
	}
	End[0] = 0;
	ml_string_t *String = new(ml_string_t);
	String->Type = StringT;
	String->Value = Value;
	String->Length = Length;
	return (ml_value_t *)String;
}

static ml_function_t StringNew[1] = {{FunctionT, ml_string_new, 0}};

typedef struct ml_method_node_t ml_method_node_t;

struct ml_method_node_t {
	ml_method_node_t *Child;
	ml_method_node_t *Next;
	const ml_type_t *Type;
	void *Data;
	ml_callback_t Callback;
};

struct ml_method_t {
	const ml_type_t *Type;
	const char *Name;
	ml_method_node_t Root[1];
};

static ml_method_node_t *ml_method_find(ml_method_node_t *Node, int Count, ml_value_t **Args) {
	if (Count == 0) return Node;
	for (const ml_type_t *Type = Args[0]->Type; Type; Type = Type->Parent) {
		for (ml_method_node_t *Test = Node->Child; Test; Test = Test->Next) {
			if (Test->Type == Type) {
				ml_method_node_t *Result = ml_method_find(Test, Count - 1, Args + 1);
				if (Result && Result->Callback) return Result;
			}
		}
	}
	return Node;
}

ml_value_t *ml_method_call(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	ml_method_t *Method = (ml_method_t *)Value;
	ml_method_node_t *Node = ml_method_find(Method->Root, Count, Args);
	if (Node->Callback) {
		return (Node->Callback)(ML, Node->Data, Count, Args);
	} else {
		return ml_error("MethodError", "no matching method found for %s", Method->Name);;
	}
}

ml_type_t MethodT[1] = {{
	FunctionT,
	ml_default_hash,
	ml_method_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

static int NumMethods = 0;
static int MaxMethods = 2;
static ml_method_t **Methods;

ml_value_t *ml_method(const char *Name) {
	if (!Name) {
		ml_method_t *Method = new(ml_method_t);
		Method->Type = MethodT;
		Method->Name = Name;
		return (ml_value_t *)Method;
	}
	int Lo = 0, Hi = NumMethods - 1;
	while (Lo <= Hi) {
		int Mid = (Lo + Hi) / 2;
		int Cmp = strcmp(Name, Methods[Mid]->Name);
		if (Cmp < 0) {
			Hi = Mid - 1;
		} else if (Cmp > 0) {
			Lo = Mid + 1;
		} else {
			return (ml_value_t *)Methods[Mid];
		}
	}
	ml_method_t *Method = new(ml_method_t);
	Method->Type = MethodT;
	Method->Name = Name;
	ml_method_t **SourceMethods = Methods;
	ml_method_t **TargetMethods = Methods;
	if (++NumMethods > MaxMethods) {
		MaxMethods += 32;
		Methods = TargetMethods = anew(ml_method_t *, MaxMethods);
		for (int I = Lo; --I >= 0;) TargetMethods[I] = SourceMethods[I];
	}
	for (int I = NumMethods; I > Lo; --I) TargetMethods[I] = SourceMethods[I - 1];
	TargetMethods[Lo] = Method;
	return (ml_value_t *)Method;
}

void ml_method_by_name(const char *Name, void *Data, ml_callback_t Callback, ...) {
	ml_method_t *Method = (ml_method_t *)ml_method(Name);
	ml_method_node_t *Node = Method->Root;
	va_list Args;
	va_start(Args, Callback);
	ml_type_t *Type;
	while ((Type = va_arg(Args, ml_type_t *))) {
		ml_method_node_t **Slot = &Node->Child;
		while (Slot[0] && Slot[0]->Type != Type) Slot = &Slot[0]->Next;
		if (Slot[0]) {
			Node = Slot[0];
		} else {
			Node = Slot[0] = new(ml_method_node_t);
			Node->Type = Type;
		}

	}
	va_end(Args);
	Node->Data = Data;
	Node->Callback = Callback;
}

void ml_method_by_value(ml_value_t *Value, void *Data, ml_callback_t Callback, ...) {
	ml_method_t *Method = (ml_method_t *)Value;
	ml_method_node_t *Node = Method->Root;
	va_list Args;
	va_start(Args, Callback);
	ml_type_t *Type;
	while ((Type = va_arg(Args, ml_type_t *))) {
		ml_method_node_t **Slot = &Node->Child;
		while (Slot[0] && Slot[0]->Type != Type) Slot = &Slot[0]->Next;
		if (Slot[0]) {
			Node = Slot[0];
		} else {
			Node = Slot[0] = new(ml_method_node_t);
			Node->Type = Type;
		}

	}
	va_end(Args);
	Node->Data = Data;
	Node->Callback = Callback;
}

struct ml_reference_t {
	const ml_type_t *Type;
	ml_value_t **Address;
	ml_value_t *Value[];
};

static ml_value_t *ml_reference_deref(ml_t *ML, ml_value_t *Ref) {
	ml_reference_t *Reference = (ml_reference_t *)Ref;
	return Reference->Address[0];
}

static ml_value_t *ml_reference_assign(ml_t *ML, ml_value_t *Ref, ml_value_t *Value) {
	ml_reference_t *Reference = (ml_reference_t *)Ref;
	return Reference->Address[0] = Value;
}

ml_type_t ReferenceT[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_reference_deref,
	ml_reference_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t *ml_reference(ml_value_t **Address) {
	ml_reference_t *Reference;
	if (Address == 0) {
		Reference = xnew(ml_reference_t, 1, ml_value_t *);
		Reference->Address = Reference->Value;
	} else {
		Reference = new(ml_reference_t);
		Reference->Address = Address;
	}
	Reference->Type = ReferenceT;
	return (ml_value_t *)Reference;
}

struct ml_list_t {
	const ml_type_t *Type;
	ml_list_node_t *Head, *Tail;
	int Length;
};

struct ml_list_node_t {
	ml_list_node_t *Next, *Prev;
	ml_value_t *Value;
};

static ml_value_t *ml_list_index(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	ml_list_t *List = (ml_list_t *)Value;
	if (Args[0]->Type != IntegerT) return ml_error("ParamError", "indices to lists must be integer");
	long Index = ((ml_integer_t *)Args[0])->Value;
	if (Index > 0) {
		for (ml_list_node_t *Node = List->Head; Node; Node = Node->Next) {
			if (--Index == 0) return ml_reference(&Node->Value);
		}
		return Nil;
	} else {
		Index = -Index;
		for (ml_list_node_t *Node = List->Tail; Node; Node = Node->Prev) {
			if (--Index == 0) return ml_reference(&Node->Value);
		}
		return Nil;
	}
}

ml_type_t ListT[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_list_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t *ml_list() {
	ml_list_t *List = new(ml_list_t);
	List->Type = ListT;
	return (ml_value_t *)List;
}

int ml_is_list(ml_value_t *Value) {
	return Value->Type == ListT;
}

int ml_list_foreach(ml_value_t *Value, void *Data, int (*callback)(ml_value_t *, void *)) {
	ml_list_t *List = (ml_list_t *)Value;
	for (ml_list_node_t *Node = List->Head; Node; Node = Node->Next) {
		if (callback(Node->Value, Data)) return 1;
	}
	return 0;
}

static ml_value_t *ml_list_new(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_list_t *List = new(ml_list_t);
	List->Type = ListT;
	ml_list_node_t **Slot = &List->Head;
	ml_list_node_t *Prev = 0;
	for (int I = 0; I < Count; ++I) {
		ml_list_node_t *Node = Slot[0] = new(ml_list_node_t);
		Node->Value = Args[I];
		Node->Prev = Prev;
		Prev = Node;
		Slot = &Node->Next;
	}
	List->Tail = Prev;
	List->Length = Count;
	return (ml_value_t *)List;
}

static ml_function_t ListNew[1] = {{FunctionT, ml_list_new, 0}};

struct ml_tree_t {
	const ml_type_t *Type;
	ml_tree_node_t *Root;
	int Size;
};

struct ml_tree_node_t {
	ml_tree_node_t *Left, *Right;
	ml_value_t *Key;
	ml_value_t *Value;
	long Hash;
	int Depth;
};

ml_value_t *ml_tree_search(ml_t *ML, ml_tree_t *Tree, ml_value_t *Key) {
	ml_tree_node_t *Node = Tree->Root;
	long Hash = ml_hash(ML, Key);
	while (Node) {
		int Compare = Hash - Node->Hash;
		if (!Compare) {
			ml_value_t *Args[2] = {Key, Node->Key};
			ml_value_t *Result = ml_method_call(ML, CompareMethod, 2, Args);
			if (Result->Type == IntegerT) {
				Compare = ((ml_integer_t *)Result)->Value;
			} else if (Result->Type == RealT) {
				Compare = ((ml_real_t *)Result)->Value;
			} else {
				return ml_error("CompareError", "comparison must return number");
			}
		}
		if (!Compare) {
			return Node->Value;
		} else {
			Node = Compare < 0 ? Node->Left : Node->Right;
		}
	}
	return Nil;
}

static int ml_tree_balance(ml_tree_node_t *Node) {
	int Delta = 0;
	if (Node->Left) Delta = Node->Left->Depth;
	if (Node->Right) Delta -= Node->Right->Depth;
	return Delta;
}

static void ml_tree_update_depth(ml_tree_node_t *Node) {
	int Depth = 0;
	if (Node->Left) Depth = Node->Left->Depth;
	if (Node->Right && Depth < Node->Right->Depth) Depth = Node->Right->Depth;
	Node->Depth = Depth + 1;
}

static void ml_tree_rotate_left(ml_tree_node_t **Slot) {
	ml_tree_node_t *Ch = Slot[0]->Right;
	Slot[0]->Right = Slot[0]->Right->Left;
	Ch->Left = Slot[0];
	ml_tree_update_depth(Slot[0]);
	Slot[0] = Ch;
	ml_tree_update_depth(Slot[0]);
}

static void ml_tree_rotate_right(ml_tree_node_t **Slot) {
	ml_tree_node_t *Ch = Slot[0]->Left;
	Slot[0]->Left = Slot[0]->Left->Right;
	Ch->Right = Slot[0];
	ml_tree_update_depth(Slot[0]);
	Slot[0] = Ch;
	ml_tree_update_depth(Slot[0]);
}

static void ml_tree_rebalance(ml_tree_node_t **Slot) {
	int Delta = ml_tree_balance(Slot[0]);
	if (Delta == 2) {
		if (ml_tree_balance(Slot[0]->Left) < 0) ml_tree_rotate_left(&Slot[0]->Left);
		ml_tree_rotate_right(Slot);
	} else if (Delta == -2) {
		if (ml_tree_balance(Slot[0]->Right) > 0) ml_tree_rotate_right(&Slot[0]->Right);
		ml_tree_rotate_left(Slot);
	}
}

static ml_value_t *ml_tree_insert_internal(ml_t *ML, ml_tree_t *Tree, ml_tree_node_t **Slot, long Hash, ml_value_t *Key, ml_value_t *Value) {
	if (!Slot[0]) {
		ml_tree_node_t *Node = Slot[0] = new(ml_tree_node_t);
		Node->Depth = 1;
		Node->Hash = Hash;
		Node->Key = Key;
		Node->Value = Value;
		return 0;
	}
	int Compare = Hash - Slot[0]->Hash;
	if (!Compare) {
		ml_value_t *Args[2] = {Key, Slot[0]->Key};
		ml_value_t *Result = ml_method_call(ML, CompareMethod, 2, Args);
		if (Result->Type == IntegerT) {
			Compare = ((ml_integer_t *)Result)->Value;
		} else if (Result->Type == RealT) {
			Compare = ((ml_real_t *)Result)->Value;
		} else {
			return ml_error("CompareError", "comparison must return number");
		}
	}
	if (!Compare) {
		ml_value_t *Old = Slot[0]->Value;
		Slot[0]->Value = Value;
		return Old;
	} else {
		ml_value_t *Old = ml_tree_insert_internal(ML, Tree, Compare < 0 ? &Slot[0]->Left : &Slot[0]->Right, Hash, Key, Value);
		ml_tree_rebalance(Slot);
		ml_tree_update_depth(Slot[0]);
		return Old;
	}
}

ml_value_t *ml_tree_insert(ml_t *ML, ml_tree_t *Tree, ml_value_t *Key, ml_value_t *Value) {
	return ml_tree_insert_internal(ML, Tree, &Tree->Root, ml_hash(ML, Key), Key, Value);
}

static void ml_tree_remove_depth_helper(ml_tree_node_t *Node) {
	if (Node) {
		ml_tree_remove_depth_helper(Node->Right);
		ml_tree_update_depth(Node);
	}
}

ml_value_t *ml_tree_remove_internal(ml_t *ML, ml_tree_t *Tree, ml_tree_node_t **Slot, long Hash, ml_value_t *Key) {
	if (!Slot[0]) return Nil;
	int Compare = Hash - Slot[0]->Hash;
	if (!Compare) {
		ml_value_t *Args[2] = {Key, Slot[0]->Key};
		ml_value_t *Result = ml_method_call(ML, CompareMethod, 2, Args);
		if (Result->Type == IntegerT) {
			Compare = ((ml_integer_t *)Result)->Value;
		} else if (Result->Type == RealT) {
			Compare = ((ml_real_t *)Result)->Value;
		} else {
			return ml_error("CompareError", "comparison must return number");
		}
	}
	ml_value_t *Removed = Nil;
	if (!Compare) {
		Removed = Slot[0]->Value;
		if (Slot[0]->Left && Slot[0]->Right) {
			ml_tree_node_t **Y = &Slot[0]->Left;
			while (Y[0]->Right) Y = &Y[0]->Right;
			Slot[0]->Key = Y[0]->Key;
			Slot[0]->Hash = Y[0]->Hash;
			Slot[0]->Value = Y[0]->Value;
			Y[0] = Y[0]->Left;
			ml_tree_remove_depth_helper(Slot[0]->Left);
		} else if (Slot[0]->Left) {
			Slot[0] = Slot[0]->Left;
		} else if (Slot[0]->Right) {
			Slot[0] = Slot[0]->Right;
		} else {
			Slot[0] = 0;
		}
	} else {
		Removed = ml_tree_remove_internal(ML, Tree, Compare < 0 ? &Slot[0]->Left : &Slot[0]->Right, Hash, Key);
	}
	if (Slot[0]) {
		ml_tree_update_depth(Slot[0]);
		ml_tree_rebalance(Slot);
	}
	return Removed;
}

ml_value_t *ml_tree_remove(ml_t *ML, ml_tree_t *Tree, ml_value_t *Key) {
	return ml_tree_remove_internal(ML, Tree, &Tree->Root, ml_hash(ML, Key), Key);
}

static ml_value_t *ml_tree_index_get(ml_t *ML, void *Data, const char *Name) {
	ml_tree_t *Tree = (ml_tree_t *)Data;
	ml_value_t *Key = (ml_value_t *)Name;
	return ml_tree_search(ML, Tree, Key);
}

static ml_value_t *ml_tree_index_set(ml_t *ML, void *Data, const char *Name, ml_value_t *Value) {
	ml_tree_t *Tree = (ml_tree_t *)Data;
	ml_value_t *Key = (ml_value_t *)Name;
	ml_tree_insert(ML, Tree, Key, Value);
	return Value;
}

static ml_value_t *ml_tree_index(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	ml_tree_t *Tree = (ml_tree_t *)Value;
	if (Count < 1) return Nil;
	ml_value_t *Key = Args[0];
	return ml_property(Tree, (const char *)Key, ml_tree_index_get, ml_tree_index_set, 0, 0);
}

static ml_value_t *ml_tree_delete(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	if (Count < 2) return Nil;
	ml_tree_t *Tree = (ml_tree_t *)Args[0];
	ml_value_t *Key = Args[1];
	return ml_tree_remove(ML, Tree, Key);
}

ml_type_t TreeT[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_tree_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t *ml_tree() {
	ml_tree_t *Tree = new(ml_tree_t);
	Tree->Type = TreeT;
	return (ml_value_t *)Tree;
}

int ml_is_tree(ml_value_t *Value) {
	return Value->Type == TreeT;
}

static int ml_tree_node_foreach(ml_tree_node_t *Node, void *Data, int (*callback)(ml_value_t *, ml_value_t *, void *)) {
	if (callback(Node->Key, Node->Value, Data)) return 1;
	if (Node->Left && ml_tree_node_foreach(Node->Left, Data, callback)) return 1;
	if (Node->Right && ml_tree_node_foreach(Node->Right, Data, callback)) return 1;
	return 0;
}

int ml_tree_foreach(ml_value_t *Value, void *Data, int (*callback)(ml_value_t *, ml_value_t *, void *)) {
	ml_tree_t *Tree = (ml_tree_t *)Value;
	return Tree->Root ? ml_tree_node_foreach(Tree->Root, Data, callback) : 0;
}

static ml_value_t *ml_tree_new(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_tree_t *Tree = new(ml_tree_t);
	Tree->Type = TreeT;
	for (int I = 0; I < Count; I += 2) ml_tree_insert(ML, Tree, Args[I], Args[I + 1]);
	return (ml_value_t *)Tree;
}

static ml_function_t TreeNew[1] = {{FunctionT, ml_tree_new, 0}};

struct ml_property_t {
	const ml_type_t *Type;
	void *Data;
	const char *Name;
	ml_getter_t Get;
	ml_setter_t Set;
	ml_getter_t Next;
	ml_getter_t Key;
};

static ml_value_t *ml_property_deref(ml_t *ML, ml_value_t *Ref) {
	ml_property_t *Property = (ml_property_t *)Ref;
	return (Property->Get)(ML, Property->Data, Property->Name);
}

static ml_value_t *ml_property_assign(ml_t *ML, ml_value_t *Ref, ml_value_t *Value) {
	ml_property_t *Property = (ml_property_t *)Ref;
	if (Property->Set) {
		return (Property->Set)(ML, Property->Data, Property->Name, Value);
	} else {
		return ml_error("TypeError", "value is not assignable");
	}
}

static ml_value_t *ml_property_next(ml_t *ML, ml_value_t *Iter) {
	ml_property_t *Property = (ml_property_t *)Iter;
	if (Property->Next) {
		return (Property->Next)(ML, Property->Data, "next");
	} else {
		return ml_error("TypeError", "value is not iterable");
	}
}

static ml_value_t *ml_property_key(ml_t *ML, ml_value_t *Iter) {
	ml_property_t *Property = (ml_property_t *)Iter;
	if (Property->Key) {
		return (Property->Key)(ML, Property->Data, "next");
	} else {
		return ml_error("TypeError", "value is not iterable");
	}
}

ml_type_t PropertyT[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_property_deref,
	ml_property_assign,
	ml_property_next,
	ml_property_key
}};

ml_value_t *ml_property(void *Data, const char *Name, ml_getter_t Get, ml_setter_t Set, ml_getter_t Next, ml_getter_t Key) {
	ml_property_t *Property = new(ml_property_t);
	Property->Type = PropertyT;
	Property->Data = Data;
	Property->Name = Name;
	Property->Get = Get;
	Property->Set = Set;
	Property->Next = Next;
	Property->Key = Key;
	return (ml_value_t *)Property;
}

typedef struct ml_closure_info_t {
	ml_inst_t *Entry;
	int FrameSize;
	int NumParams;
	unsigned char Hash[SHA256_BLOCK_SIZE];
} ml_closure_info_t;

struct ml_closure_t {
	const ml_type_t *Type;
	ml_closure_info_t *Info;
	ml_value_t *UpValues[];
};

void ml_closure_hash(ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	ml_closure_t *Closure = (ml_closure_t *)Value;
	memcpy(Hash, Closure->Info->Hash, SHA256_BLOCK_SIZE);
}

struct ml_frame_t {
	ml_inst_t *OnError;
	ml_value_t **UpValues;
	ml_value_t **Top;
	ml_value_t *Stack[];
};

typedef union {
	ml_inst_t *Inst;
	int Index;
	int Count;
	ml_value_t *Value;
	const char *Name;
	ml_closure_info_t *ClosureInfo;
} ml_param_t;

struct ml_inst_t {
	ml_inst_t *(*run)(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame);
	ml_source_t Source;
	ml_param_t Params[];
};

static ml_value_t *ml_closure_call(ml_t *ML, ml_value_t *Value, int Count, ml_value_t **Args) {
	ml_closure_t *Closure = (ml_closure_t *)Value;
	ml_closure_info_t *Info = Closure->Info;
	ml_frame_t *Frame = xnew(ml_frame_t, Info->FrameSize, ml_value_t *);
	int NumParams = Info->NumParams;
	int VarArgs = 0;
	if (NumParams < 0) {
		VarArgs = 1;
		NumParams = ~NumParams;
	}
	if (Count > NumParams) Count = NumParams;
	for (int I = 0; I < Count; ++I) {
		ml_reference_t *Local = xnew(ml_reference_t, 1, ml_value_t *);
		Local->Type = ReferenceT;
		Local->Address = Local->Value;
		ml_value_t *Value = Args[I];
		Local->Value[0] = Value->Type->deref(ML, Value);
		Frame->Stack[I] = (ml_value_t *)Local;
	}
	for (int I = Count; I < NumParams; ++I) {
		ml_reference_t *Local = xnew(ml_reference_t, 1, ml_value_t *);
		Local->Type = ReferenceT;
		Local->Address = Local->Value;
		Local->Value[0] = Nil;
		Frame->Stack[I] = (ml_value_t *)Local;
	}
	if (VarArgs) {
		ml_reference_t *Local = xnew(ml_reference_t, 1, ml_value_t *);
		Local->Type = ReferenceT;
		Local->Address = Local->Value;
		ml_list_t *Rest = new(ml_list_t);
		int Length = 0;
		ml_list_node_t **Next = &Rest->Head;
		ml_list_node_t *Prev = Next[0] = 0;
		for (int I = NumParams; I < Count; ++I) {
			ml_list_node_t *Node = new(ml_list_node_t);
			Node->Prev = Prev;
			Next[0] = Prev = Node;
			Next = &Node->Next;
			++Length;
		}
		Rest->Tail = Prev;
		Rest->Length = Length;
		Local->Value[0] = (ml_value_t *)Rest;
		Frame->Stack[NumParams] = (ml_value_t *)Local;
	}
	Frame->Top = Frame->Stack + NumParams + VarArgs;
	Frame->OnError = 0;
	Frame->UpValues = Closure->UpValues;
	ml_inst_t *Inst = Closure->Info->Entry;
	while (Inst) Inst = Inst->run(ML, Inst, Frame);
	return Frame->Top[-1];
}



ml_type_t ClosureT[1] = {{
	FunctionT,
	ml_default_hash,
	ml_closure_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

struct ml_error_t {
	const ml_type_t *Type;
	const char *Error;
	const char *Message;
	ml_source_t Trace[MAX_TRACE];
};

ml_type_t ErrorT[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

ml_value_t *ml_error(const char *Error, const char *Format, ...) {
	va_list Args;
	va_start(Args, Format);
	char *Message;
	vasprintf(&Message, Format, Args);
	va_end(Args);
	ml_error_t *Value = new(ml_error_t);
	Value->Type = ErrorT;
	Value->Error = Error;
	Value->Message = Message;
	memset(Value->Trace, 0, sizeof(Value->Trace));
	return (ml_value_t *)Value;
}

static void ml_error_trace_add(ml_value_t *Value, ml_source_t Source) {
	ml_error_t *Error = (ml_error_t *)Value;
	for (int I = 0; I < MAX_TRACE; ++I) if (!Error->Trace[I].Name) {
		Error->Trace[I] = Source;
		return;
	}
}

int ml_is_error(ml_value_t *Value) {
	return Value->Type == ErrorT;
}

const char *ml_error_type(ml_value_t *Value) {
	return ((ml_error_t *)Value)->Error;
}

const char *ml_error_message(ml_value_t *Value) {
	return ((ml_error_t *)Value)->Message;
}

int ml_error_trace(ml_value_t *Value, int Level, const char **Source, int *Line) {
	ml_error_t *Error = (ml_error_t *)Value;
	if (Level >= MAX_TRACE) return 0;
	if (!Error->Trace[Level].Name) return 0;
	Source[0] = Error->Trace[Level].Name;
	Line[0] = Error->Trace[Level].Line;
	return 1;
}

#define ml_arith_method_integer(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _integer(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_integer_t *IntegerA = (ml_integer_t *)Args[0]; \
		return ml_integer(SYMBOL(IntegerA->Value)); \
	}

#define ml_arith_method_integer_integer(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _integer_integer(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_integer_t *IntegerA = (ml_integer_t *)Args[0]; \
		ml_integer_t *IntegerB = (ml_integer_t *)Args[1]; \
		return ml_integer(IntegerA->Value SYMBOL IntegerB->Value); \
	}

#define ml_arith_method_real(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _real(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_real_t *RealA = (ml_real_t *)Args[0]; \
		return ml_real(SYMBOL(RealA->Value)); \
	}

#define ml_arith_method_real_real(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _real_real(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_real_t *RealA = (ml_real_t *)Args[0]; \
		ml_real_t *RealB = (ml_real_t *)Args[1]; \
		return ml_real(RealA->Value SYMBOL RealB->Value); \
	}

#define ml_arith_method_real_integer(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _real_integer(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_real_t *RealA = (ml_real_t *)Args[0]; \
		ml_integer_t *IntegerB = (ml_integer_t *)Args[1]; \
		return ml_real(RealA->Value SYMBOL IntegerB->Value); \
	}

#define ml_arith_method_integer_real(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _integer_real(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_integer_t *IntegerA = (ml_integer_t *)Args[0]; \
		ml_real_t *RealB = (ml_real_t *)Args[1]; \
		return ml_real(IntegerA->Value SYMBOL RealB->Value); \
	}

#define ml_arith_method_number(NAME, SYMBOL) \
	ml_arith_method_integer(NAME, SYMBOL) \
	ml_arith_method_real(NAME, SYMBOL)

#define ml_arith_method_number_number(NAME, SYMBOL) \
	ml_arith_method_integer_integer(NAME, SYMBOL) \
	ml_arith_method_real_real(NAME, SYMBOL) \
	ml_arith_method_real_integer(NAME, SYMBOL) \
	ml_arith_method_integer_real(NAME, SYMBOL)

ml_arith_method_number(neg, -)
ml_arith_method_number_number(add, +)
ml_arith_method_number_number(sub, -)
ml_arith_method_number_number(mul, *)
ml_arith_method_number_number(div, /)

ml_arith_method_integer_integer(mod, %)

#define ml_comp_method_integer_integer(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _integer_integer(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_integer_t *IntegerA = (ml_integer_t *)Args[0]; \
		ml_integer_t *IntegerB = (ml_integer_t *)Args[1]; \
		return IntegerA->Value SYMBOL IntegerB->Value ? Args[1] : Nil; \
	}

#define ml_comp_method_real_real(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _real_real(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_real_t *RealA = (ml_real_t *)Args[0]; \
		ml_real_t *RealB = (ml_real_t *)Args[1]; \
		return RealA->Value SYMBOL RealB->Value ? Args[1] : Nil; \
	}

#define ml_comp_method_real_integer(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _real_integer(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_real_t *RealA = (ml_real_t *)Args[0]; \
		ml_integer_t *IntegerB = (ml_integer_t *)Args[1]; \
		return RealA->Value SYMBOL IntegerB->Value ? Args[1] : Nil; \
	}

#define ml_comp_method_integer_real(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _integer_real(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_integer_t *IntegerA = (ml_integer_t *)Args[0]; \
		ml_real_t *RealB = (ml_real_t *)Args[1]; \
		return IntegerA->Value SYMBOL RealB->Value ? Args[1] : Nil; \
	}

#define ml_comp_method_number_number(NAME, SYMBOL) \
	ml_comp_method_integer_integer(NAME, SYMBOL) \
	ml_comp_method_real_real(NAME, SYMBOL) \
	ml_comp_method_real_integer(NAME, SYMBOL) \
	ml_comp_method_integer_real(NAME, SYMBOL)

ml_comp_method_number_number(eq, ==)
ml_comp_method_number_number(neq, !=)
ml_comp_method_number_number(les, <)
ml_comp_method_number_number(gre, >)
ml_comp_method_number_number(leq, <=)
ml_comp_method_number_number(geq, >=)

typedef struct ml_integer_range_t {
	const ml_type_t *Type;
	ml_integer_t *Current;
	long Step, Limit;
} ml_integer_range_t;

static ml_value_t *ml_integer_range_deref(ml_t *ML, ml_value_t *Ref) {
	ml_integer_range_t *Range = (ml_integer_range_t *)Ref;
	return (ml_value_t *)Range->Current;
}

static ml_value_t *ml_integer_range_next(ml_t *ML, ml_value_t *Ref) {
	ml_integer_range_t *Range = (ml_integer_range_t *)Ref;
	if (Range->Current->Value >= Range->Limit) {
		return Nil;
	} else {
		Range->Current = (ml_integer_t *)ml_integer(Range->Current->Value + Range->Step);
		return Ref;
	}
}

ml_type_t MLIntegerRange[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_integer_range_deref,
	ml_default_assign,
	ml_integer_range_next,
	ml_default_key
}};

static ml_value_t *ml_range_integer_integer(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_integer_t *IntegerA = (ml_integer_t *)Args[0];
	ml_integer_t *IntegerB = (ml_integer_t *)Args[1];
	ml_integer_range_t *Range = new(ml_integer_range_t);
	Range->Type = MLIntegerRange;
	Range->Current = IntegerA;
	Range->Limit = IntegerB->Value;
	Range->Step = 1;
	return (ml_value_t *)Range;
}

#define ml_methods_add_number_number(NAME, SYMBOL) \
	ml_method_by_name(#SYMBOL, 0, ml_ ## NAME ## _integer_integer, IntegerT, IntegerT, 0); \
	ml_method_by_name(#SYMBOL, 0, ml_ ## NAME ## _real_real, RealT, RealT, 0); \
	ml_method_by_name(#SYMBOL, 0, ml_ ## NAME ## _real_integer, RealT, IntegerT, 0); \
	ml_method_by_name(#SYMBOL, 0, ml_ ## NAME ## _integer_real, IntegerT, RealT, 0)

static ml_value_t *ml_integer_to_string(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_integer_t *Integer = (ml_integer_t *)Args[0];
	ml_string_t *String = new(ml_string_t);
	String->Type = StringT;
	String->Length = asprintf((char **)&String->Value, "%d", Integer->Value);
	return (ml_value_t *)String;
}

static ml_value_t *ml_real_to_string(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_real_t *Real = (ml_real_t *)Args[0];
	ml_string_t *String = new(ml_string_t);
	String->Type = StringT;
	String->Length = asprintf((char **)&String->Value, "%f", Real->Value);
	return (ml_value_t *)String;
}

static ml_value_t *ml_identity(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	return Args[0];
}

static ml_value_t *ml_compare_string_string(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_string_t *StringA = (ml_string_t *)Args[0];
	ml_string_t *StringB = (ml_string_t *)Args[1];
	return ml_integer(strcmp(StringA->Value, StringB->Value));
}

#define ml_comp_method_string_string(NAME, SYMBOL) \
	static ml_value_t *ml_ ## NAME ## _string_string(ml_t *ML, void *Data, int Count, ml_value_t **Args) { \
		ml_string_t *StringA = (ml_string_t *)Args[0]; \
		ml_string_t *StringB = (ml_string_t *)Args[1]; \
		return strcmp(StringA->Value, StringB->Value) SYMBOL 0 ? Args[1] : Nil; \
	}

ml_comp_method_string_string(eq, ==)
ml_comp_method_string_string(neq, !=)
ml_comp_method_string_string(les, <)
ml_comp_method_string_string(gre, >)
ml_comp_method_string_string(leq, <=)
ml_comp_method_string_string(geq, >=)

static ml_value_t *ml_compare_any_any(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	if (Args[0] < Args[1]) return ml_integer(-1);
	if (Args[0] > Args[1]) return ml_integer(1);
	return ml_integer(0);
}

typedef struct ml_list_iter_t {
	const ml_type_t *Type;
	ml_list_node_t *Node;
	long Index;
} ml_list_iter_t;

static ml_value_t *ml_list_iter_deref(ml_t *ML, ml_value_t *Ref) {
	ml_list_iter_t *Iter = (ml_list_iter_t *)Ref;
	return Iter->Node->Value;
}

static ml_value_t *ml_list_iter_assign(ml_t *ML, ml_value_t *Ref, ml_value_t *Value) {
	ml_list_iter_t *Iter = (ml_list_iter_t *)Ref;
	return Iter->Node->Value = Value;
}

static ml_value_t *ml_list_iter_next(ml_t *ML, ml_value_t *Ref) {
	ml_list_iter_t *Iter = (ml_list_iter_t *)Ref;
	if (Iter->Node->Next) {
		++Iter->Index;
		Iter->Node = Iter->Node->Next;
		return Ref;
	} else {
		return Nil;
	}
}

static ml_value_t *ml_list_iter_key(ml_t *ML, ml_value_t *Ref) {
	ml_list_iter_t *Iter = (ml_list_iter_t *)Ref;
	return ml_integer(Iter->Index);
}

ml_type_t MLListIter[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_list_iter_deref,
	ml_list_iter_assign,
	ml_list_iter_next,
	ml_list_iter_key
}};

static ml_value_t *ml_list_values(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_list_t *List = (ml_list_t *)Args[0];
	if (List->Head) {
		ml_list_iter_t *Iter = new(ml_list_iter_t);
		Iter->Type = MLListIter;
		Iter->Node = List->Head;
		Iter->Index = 1;
		return (ml_value_t *)Iter;
	} else {
		return Nil;
	}
}

static ml_value_t *ml_list_push(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_list_t *List = (ml_list_t *)Args[0];
	ml_list_node_t **Slot = List->Head ? &List->Head->Prev : &List->Tail;
	ml_list_node_t *Next = List->Head;
	for (int I = Count; --I >= 1;) {
		ml_list_node_t *Node = Slot[0] = new(ml_list_node_t);
		Node->Value = Args[I];
		Node->Next = Next;
		Next = Node;
		Slot = &Node->Prev;
	}
	List->Head = Next;
	List->Length += Count - 1;
	return (ml_value_t *)List;
}

static ml_value_t *ml_list_put(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_list_t *List = (ml_list_t *)Args[0];
	ml_list_node_t **Slot = List->Tail ? &List->Tail->Next : &List->Head;
	ml_list_node_t *Prev = List->Tail;
	for (int I = 1; I < Count; ++I) {
		ml_list_node_t *Node = Slot[0] = new(ml_list_node_t);
		Node->Value = Args[I];
		Node->Prev = Prev;
		Prev = Node;
		Slot = &Node->Next;
	}
	List->Tail = Prev;
	List->Length += Count - 1;
	return (ml_value_t *)List;
}

static ml_value_t *ml_list_pop(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_list_t *List = (ml_list_t *)Args[0];
	ml_list_node_t *Node = List->Head;
	if (Node) {
		if (!(List->Head = Node->Next)) List->Tail = 0;
		--List->Length;
		return Node->Value;
	} else {
		return Nil;
	}
}

static ml_value_t *ml_list_pull(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_list_t *List = (ml_list_t *)Args[0];
	ml_list_node_t *Node = List->Tail;
	if (Node) {
		if (!(List->Tail = Node->Next)) List->Head = 0;
		--List->Length;
		return Node->Value;
	} else {
		return Nil;
	}
}

#define ML_TREE_MAX_DEPTH 32

typedef struct ml_tree_iter_t {
	const ml_type_t *Type;
	ml_tree_node_t *Node;
	ml_tree_node_t *Stack[ML_TREE_MAX_DEPTH];
	int Top;
} ml_tree_iter_t;

static ml_value_t *ml_tree_iter_deref(ml_t *ML, ml_value_t *Ref) {
	ml_tree_iter_t *Iter = (ml_tree_iter_t *)Ref;
	return Iter->Node->Value;
}

static ml_value_t *ml_tree_iter_assign(ml_t *ML, ml_value_t *Ref, ml_value_t *Value) {
	ml_tree_iter_t *Iter = (ml_tree_iter_t *)Ref;
	return Iter->Node->Value = Value;
}

static ml_value_t *ml_tree_iter_next(ml_t *ML, ml_value_t *Ref) {
	ml_tree_iter_t *Iter = (ml_tree_iter_t *)Ref;
	ml_tree_node_t *Node = Iter->Node;
	if (Node->Left) {
		if (Node->Right) Iter->Stack[Iter->Top++] = Node->Right;
		Iter->Node = Node->Left;
		return Ref;
	} else if (Node->Right) {
		Iter->Node = Node->Right;
		return Ref;
	} else if (Iter->Top > 0) {
		Iter->Node = Iter->Stack[--Iter->Top];
		return Ref;
	} else {
		return Nil;
	}
}

static ml_value_t *ml_tree_iter_key(ml_t *ML, ml_value_t *Ref) {
	ml_tree_iter_t *Iter = (ml_tree_iter_t *)Ref;
	return Iter->Node->Key;
}

ml_type_t MLTreeIter[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_tree_iter_deref,
	ml_tree_iter_assign,
	ml_tree_iter_next,
	ml_tree_iter_key
}};

static ml_value_t *ml_tree_values(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_tree_t *Tree = (ml_tree_t *)Args[0];
	if (Tree->Root) {
		ml_tree_iter_t *Iter = new(ml_tree_iter_t);
		Iter->Type = MLTreeIter;
		Iter->Node = Tree->Root;
		Iter->Top = 0;
		return (ml_value_t *)Iter;
	} else {
		return Nil;
	}
}

static void ml_init() {
	Methods = anew(ml_method_t *, MaxMethods);
	CompareMethod = ml_method("?");
	ml_method_by_name("-", 0, ml_neg_integer, IntegerT, 0);
	ml_method_by_name("-", 0, ml_neg_real, RealT, 0);
	ml_methods_add_number_number(sub, ?);
	ml_methods_add_number_number(add, +);
	ml_methods_add_number_number(sub, -);
	ml_methods_add_number_number(mul, *);
	ml_methods_add_number_number(div, /);
	ml_methods_add_number_number(eq, =);
	ml_methods_add_number_number(neq, !=);
	ml_methods_add_number_number(les, <);
	ml_methods_add_number_number(gre, >);
	ml_methods_add_number_number(leq, <=);
	ml_methods_add_number_number(geq, >=);
	ml_method_by_name("%", 0, ml_mod_integer_integer, IntegerT, IntegerT, 0);
	ml_method_by_name("->", 0, ml_range_integer_integer, IntegerT, IntegerT, 0);
	ml_method_by_name("?", 0, ml_compare_string_string, StringT, StringT, 0);
	ml_method_by_name("=", 0, ml_eq_string_string, StringT, StringT, 0);
	ml_method_by_name("!=", 0, ml_neq_string_string, StringT, StringT, 0);
	ml_method_by_name("<", 0, ml_les_string_string, StringT, StringT, 0);
	ml_method_by_name(">", 0, ml_gre_string_string, StringT, StringT, 0);
	ml_method_by_name("<=", 0, ml_leq_string_string, StringT, StringT, 0);
	ml_method_by_name(">=", 0, ml_geq_string_string, StringT, StringT, 0);
	ml_method_by_name("?", 0, ml_compare_any_any, AnyT, AnyT, 0);
	ml_method_by_name("values", 0, ml_list_values, ListT, 0);
	ml_method_by_name("push", 0, ml_list_push, ListT, 0);
	ml_method_by_name("put", 0, ml_list_put, ListT, 0);
	ml_method_by_name("pop", 0, ml_list_pop, ListT, 0);
	ml_method_by_name("pull", 0, ml_list_pull, ListT, 0);
	ml_method_by_name("values", 0, ml_tree_values, TreeT, 0);
	ml_method_by_name("delete", 0, ml_tree_delete, TreeT, 0);
	ml_method_by_name("string", 0, ml_nil_to_string, NilT, 0);
	ml_method_by_name("string", 0, ml_integer_to_string, IntegerT, 0);
	ml_method_by_name("string", 0, ml_real_to_string, RealT, 0);
	ml_method_by_name("string", 0, ml_identity, StringT, 0);
	// TODO: Implement other methods
}

ml_t *ml_new(void *Data, ml_getter_t Get) {
	static int Initialized = 0;
	if (!Initialized) {
		Initialized = 1;
		ml_init();
	}
	ml_t *ML = new(ml_t);
	ML->Data = Data;
	ML->Get = Get;
	return ML;
}

ml_inst_t *mli_push_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	(++Frame->Top)[-1] = Inst->Params[1].Value;
	return Inst->Params[0].Inst;
}

ml_inst_t *mli_pop_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	(--Frame->Top)[0] = 0;
	return Inst->Params[0].Inst;
}

ml_inst_t *mli_enter_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	for (int I = Inst->Params[1].Count; --I >= 0;) {
		ml_reference_t *Local = xnew(ml_reference_t, 1, ml_value_t *);
		Local->Type = ReferenceT;
		Local->Address = Local->Value;
		Local->Value[0] = Nil;
		(++Frame->Top)[-1] = (ml_value_t *)Local;
	}
	return Inst->Params[0].Inst;
}

ml_inst_t *mli_var_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	ml_reference_t *Local = (ml_reference_t *)Frame->Stack[Inst->Params[1].Index];
	Local->Value[0] = Frame->Top[-1];
	return Inst->Params[0].Inst;
}

ml_inst_t *mli_exit_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	ml_value_t *Value = Frame->Top[-1];
	for (int I = Inst->Params[1].Count; --I >= 0;) (--Frame->Top)[0] = 0;
	Frame->Top[-1] = Value;
	return Inst->Params[0].Inst;
}

ml_inst_t *mli_call_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	int Count = Inst->Params[1].Count;
	ml_value_t *Function = Frame->Top[~Count];
	ml_value_t **Args = Frame->Top - Count;
	ml_value_t *Result = ml_call(ML, Function, Count, Args);
	for (int I = Count; --I >= 0;) (--Frame->Top)[0] = 0;
	Frame->Top[-1] = Result;
	if (Result->Type == ErrorT) {
		ml_error_trace_add(Frame->Top[-1], Inst->Source);
		return Frame->OnError;
	} else {
		return Inst->Params[0].Inst;
	}
}

ml_inst_t *mli_index_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	int Count = Inst->Params[1].Count;
	ml_value_t *Function = Frame->Top[~Count];
	ml_value_t **Args = Frame->Top - Count;
	ml_value_t *Result = ml_index(ML, Function, Count, Args);
	for (int I = Count; --I >= 0;) (--Frame->Top)[0] = 0;
	Frame->Top[-1] = Result;
	if (Result->Type == ErrorT) {
		ml_error_trace_add(Frame->Top[-1], Inst->Source);
		return Frame->OnError;
	} else {
		return Inst->Params[0].Inst;
	}
}

ml_inst_t *mli_const_call_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	int Count = Inst->Params[1].Count;
	ml_value_t *Function = Inst->Params[2].Value;
	ml_value_t **Args = Frame->Top - Count;
	ml_value_t *Result = ml_call(ML, Function, Count, Args);
	if (Count == 0) {
		++Frame->Top;
	} else {
		for (int I = Count - 1; --I >= 0;) (--Frame->Top)[0] = 0;
	}
	Frame->Top[-1] = Result;
	if (Result->Type == ErrorT) {
		ml_error_trace_add(Frame->Top[-1], Inst->Source);
		return Frame->OnError;
	} else {
		return Inst->Params[0].Inst;
	}
}

ml_inst_t *mli_assign_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	ml_value_t *Value = (--Frame->Top)[0];
	ml_value_t *Ref = Frame->Top[-1];
	ml_value_t *Result = Frame->Top[-1] = Ref->Type->assign(ML, Ref, Value);
	if (Result->Type == ErrorT) {
		ml_error_trace_add(Frame->Top[-1], Inst->Source);
		return Frame->OnError;
	} else {
		return Inst->Params[0].Inst;
	}
}

ml_inst_t *mli_jump_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	return Inst->Params[0].Inst;
}

ml_inst_t *mli_if_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	ml_value_t *Value = Frame->Top[-1];
	(--Frame->Top)[0] = 0;
	if (Value == Nil) {
		return Inst->Params[0].Inst;
	} else {
		return Inst->Params[1].Inst;
	}
}

ml_inst_t *mli_start_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	ml_value_t *Iter = Frame->Top[-1];
	if (Iter->Type == ErrorT) {
		ml_error_trace_add(Frame->Top[-1], Inst->Source);
		return Frame->OnError;
	} else if (Iter == Nil) {
		return Inst->Params[0].Inst;
	} else {
		return Inst->Params[1].Inst;
	}
}

ml_inst_t *mli_next_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	ml_value_t *Iter = Frame->Top[-1];
	Frame->Top[-1] = Iter = Iter->Type->next(ML, Iter);
	if (Iter->Type == ErrorT) {
		ml_error_trace_add(Frame->Top[-1], Inst->Source);
		return Frame->OnError;
	} else if (Iter == Nil) {
		return Inst->Params[0].Inst;
	} else {
		return Inst->Params[1].Inst;
	}
}

ml_inst_t *mli_key_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	ml_value_t *Iter = Frame->Top[-1];
	ml_value_t *Key = (++Frame->Top)[-1] = Iter->Type->key(ML, Iter);
	if (Key->Type == ErrorT) {
		ml_error_trace_add(Frame->Top[-1], Inst->Source);
		return Frame->OnError;
	} else {
		return Inst->Params[0].Inst;
	}
}

ml_inst_t *mli_local_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	int Index = Inst->Params[1].Index;
	if (Index < 0) {
		(++Frame->Top)[-1] = Frame->UpValues[~Index];
	} else {
		(++Frame->Top)[-1] = Frame->Stack[Index];
	}
	return Inst->Params[0].Inst;
}

ml_inst_t *mli_closure_run(ml_t *ML, ml_inst_t *Inst, ml_frame_t *Frame) {
	// closure <entry> <frame_size> <num_params> <num_upvalues> <upvalue_1> ...
	ml_closure_t *Closure = xnew(ml_closure_t, Inst->Params[4].Count, ml_value_t *);
	Closure->Type = ClosureT;
	Closure->Info = Inst->Params[1].ClosureInfo;
	for (int I = 0; I < Inst->Params[2].Count; ++I) {
		int Index = Inst->Params[3 + I].Index;
		if (Index < 0) {
			Closure->UpValues[I] = Frame->UpValues[~Index];
		} else {
			Closure->UpValues[I] = Frame->Stack[Index];
		}
	}
	(++Frame->Top)[-1] = (ml_value_t *)Closure;
	return Inst->Params[0].Inst;
}

typedef struct mlc_expr_t mlc_expr_t;
typedef struct mlc_scanner_t mlc_scanner_t;
typedef struct mlc_function_t mlc_function_t;
typedef struct mlc_decl_t mlc_decl_t;
typedef struct mlc_loop_t mlc_loop_t;
typedef struct mlc_upvalue_t mlc_upvalue_t;

typedef struct { ml_inst_t *Start, *Exits; } mlc_compiled_t;

struct mlc_function_t {
	ml_t *ML;
	mlc_function_t *Up;
	mlc_decl_t *Decls;
	mlc_loop_t *Loop;
	mlc_upvalue_t *UpValues;
	int Top, Size, Self;
};

struct mlc_decl_t {
	mlc_decl_t *Next;
	const char *Ident;
	int Index;
};

struct mlc_loop_t {
	mlc_loop_t *Up;
	mlc_compiled_t Compiled;
	int Top;
};

struct mlc_upvalue_t {
	mlc_upvalue_t *Next;
	mlc_decl_t *Decl;
	int Index;
};

#define MLC_EXPR_FIELDS(TYPE) \
	mlc_compiled_t (*compile)(mlc_function_t *, mlc_ ## TYPE ## _expr_t *, SHA256_CTX *HashContext); \
	mlc_expr_t *Next; \
	ml_source_t Source

struct mlc_expr_t {
	mlc_compiled_t (*compile)(mlc_function_t *, mlc_expr_t *, SHA256_CTX *HashContext);
	mlc_expr_t *Next;
	ml_source_t Source;
};

static inline ml_inst_t *ml_inst_new(int N, ml_source_t Source) {
	ml_inst_t *Inst = xnew(ml_inst_t, N, ml_param_t);
	Inst->Source = Source;
	return Inst;
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define ML_COMPILE_HASH sha256_update(HashContext, __FILE__ TOSTRING(__LINE__), strlen(__FILE__ TOSTRING(__LINE__)));

static inline mlc_compiled_t ml_compile(mlc_function_t *Function, mlc_expr_t *Expr, SHA256_CTX *HashContext) {
	if (Expr) {
		return Expr->compile(Function, Expr, HashContext);
	} else {
		ML_COMPILE_HASH
		ml_inst_t *NilInst = ml_inst_new(1, (ml_source_t){"<internal>", 0});
		NilInst->run = mli_push_run;
		NilInst->Params[1].Value = Nil;
		return (mlc_compiled_t){NilInst, NilInst};
	}
}

static inline void ml_connect(ml_inst_t *Exits, ml_inst_t *Start) {
	for (ml_inst_t *Exit = Exits; Exit;) {
		ml_inst_t *NextExit = Exit->Params[0].Inst;
		Exit->Params[0].Inst = Start;
		Exit = NextExit;
	}
}

typedef struct mlc_if_expr_t mlc_if_expr_t;
typedef struct mlc_if_case_t mlc_if_case_t;
typedef struct mlc_parent_expr_t mlc_parent_expr_t;
typedef struct mlc_fun_expr_t mlc_fun_expr_t;
typedef struct mlc_decl_expr_t mlc_decl_expr_t;
typedef struct mlc_dot_expr_t mlc_dot_expr_t;
typedef struct mlc_value_expr_t mlc_value_expr_t;
typedef struct mlc_ident_expr_t mlc_ident_expr_t;
typedef struct mlc_const_call_expr_t mlc_const_call_expr_t;

typedef struct mlc_decl_t mlc_decl_t;

struct mlc_if_case_t {
	mlc_if_case_t *Next;
	ml_source_t Source;
	mlc_expr_t *Condition;
	mlc_expr_t *Body;
};

struct mlc_if_expr_t {
	MLC_EXPR_FIELDS(if);
	mlc_if_case_t *Cases;
	mlc_expr_t *Else;
};

static mlc_compiled_t ml_if_expr_compile(mlc_function_t *Function, mlc_if_expr_t *Expr, SHA256_CTX *HashContext) {
	mlc_if_case_t *Case = Expr->Cases;
	mlc_compiled_t Compiled = ml_compile(Function, Case->Condition, HashContext);
	mlc_compiled_t BodyCompiled = ml_compile(Function, Case->Body, HashContext);
	ML_COMPILE_HASH
	ml_inst_t *IfInst = ml_inst_new(2, Expr->Source);
	IfInst->run = mli_if_run;
	IfInst->Params[0].Inst = BodyCompiled.Exits;
	IfInst->Params[1].Inst = BodyCompiled.Start;
	ml_connect(Compiled.Exits, IfInst);
	Compiled.Exits = IfInst;
	while ((Case = Case->Next)) {
		Compiled.Exits = IfInst->Params[0].Inst;
		mlc_compiled_t ConditionCompiled = ml_compile(Function, Case->Condition, HashContext);
		IfInst->Params[0].Inst = ConditionCompiled.Start;
		BodyCompiled = ml_compile(Function, Case->Body, HashContext);
		ml_inst_t **Slot = &Compiled.Exits;
		while (Slot[0]) Slot = &Slot[0]->Params[0].Inst;
		Slot[0] = BodyCompiled.Exits;
		ML_COMPILE_HASH
		IfInst = ml_inst_new(2, Case->Source);
		IfInst->run = mli_if_run;
		IfInst->Params[0].Inst = Compiled.Exits;
		IfInst->Params[1].Inst = BodyCompiled.Start;
		ml_connect(ConditionCompiled.Exits, IfInst);
		Compiled.Exits = IfInst;
	}
	if (Expr->Else) {
		Compiled.Exits = IfInst->Params[0].Inst;
		mlc_compiled_t BodyCompiled = ml_compile(Function, Expr->Else, HashContext);
		IfInst->Params[0].Inst = BodyCompiled.Start;
		ml_inst_t **Slot = &Compiled.Exits;
		while (Slot[0]) Slot = &Slot[0]->Params[0].Inst;
		Slot[0] = BodyCompiled.Exits;
	}
	return Compiled;
}

struct mlc_parent_expr_t {
	MLC_EXPR_FIELDS(parent);
	mlc_expr_t *Child;
};

static mlc_compiled_t ml_loop_expr_compile(mlc_function_t *Function, mlc_parent_expr_t *Expr, SHA256_CTX *HashContext) {
	ML_COMPILE_HASH
	ml_inst_t *LoopInst = ml_inst_new(1, Expr->Source);
	LoopInst->run = mli_jump_run;
	mlc_loop_t Loop = {Function->Loop, {LoopInst, 0}, Function->Top + 1};
	Function->Loop = &Loop;
	mlc_compiled_t Compiled = ml_compile(Function, Expr->Child, HashContext);
	LoopInst->Params[0].Inst = Compiled.Start;
	ml_connect(Compiled.Exits, LoopInst);
	Function->Loop = Loop.Up;
	Function->Top = Loop.Top;
	return Loop.Compiled;
}

static mlc_compiled_t ml_next_expr_compile(mlc_function_t *Function, mlc_expr_t *Expr, SHA256_CTX *HashContext) {
	ML_COMPILE_HASH
	ml_inst_t *PopInst = ml_inst_new(1, Expr->Source);
	PopInst->run = mli_pop_run;
	PopInst->Params[0].Inst = Function->Loop->Compiled.Start;
	if (Function->Top > Function->Loop->Top) {
		ML_COMPILE_HASH
		ml_inst_t *ExitInst = ml_inst_new(2, Expr->Source);
		ExitInst->run = mli_exit_run;
		ExitInst->Params[0].Inst = PopInst;
		ExitInst->Params[1].Count = Function->Top - Function->Loop->Top;
		PopInst = ExitInst;
	}
	return (mlc_compiled_t){PopInst, 0};
}

static mlc_compiled_t ml_exit_expr_compile(mlc_function_t *Function, mlc_parent_expr_t *Expr, SHA256_CTX *HashContext) {
	mlc_compiled_t Compiled = ml_compile(Function, Expr->Child, HashContext);
	if (Function->Top > Function->Loop->Top) {
		ML_COMPILE_HASH
		ml_inst_t *ExitInst = ml_inst_new(2, Expr->Source);
		ExitInst->run = mli_exit_run;
		ExitInst->Params[1].Count = Function->Top - Function->Loop->Top;
		ml_connect(Compiled.Exits, ExitInst);
		Compiled.Exits = ExitInst;
	}
	if (Compiled.Exits) {
		Compiled.Exits->Params[0].Inst = Function->Loop->Compiled.Exits;
		Function->Loop->Compiled.Exits = Compiled.Exits;
		Compiled.Exits = 0;
	}
	return Compiled;
}

static mlc_compiled_t ml_return_expr_compile(mlc_function_t *Function, mlc_parent_expr_t *Expr, SHA256_CTX *HashContext) {
	mlc_compiled_t Compiled = ml_compile(Function, Expr->Child, HashContext);
	ml_connect(Compiled.Exits, 0);
	Compiled.Exits = 0;
	return Compiled;
}

struct mlc_decl_expr_t {
	MLC_EXPR_FIELDS(decl);
	mlc_decl_t *Decl;
	mlc_expr_t *Child;
};

static mlc_compiled_t ml_var_expr_compile(mlc_function_t *Function, mlc_decl_expr_t *Expr, SHA256_CTX *HashContext) {
	mlc_compiled_t Compiled = ml_compile(Function, Expr->Child, HashContext);
	ML_COMPILE_HASH
	ml_inst_t *VarInst = ml_inst_new(2, Expr->Source);
	VarInst->run = mli_var_run;
	VarInst->Params[1].Index = Expr->Decl->Index;
	ml_connect(Compiled.Exits, VarInst);
	Compiled.Exits = VarInst;
	return Compiled;
}

static mlc_compiled_t ml_with_expr_compile(mlc_function_t *Function, mlc_decl_expr_t *Expr, SHA256_CTX *HashContext) {
	int OldTop = Function->Top + 1;
	mlc_decl_t *OldScope = Function->Decls;
	mlc_expr_t *Child = Expr->Child;
	mlc_compiled_t Compiled = ml_compile(Function, Child, HashContext);
	mlc_decl_t *Decl = Expr->Decl;
	Decl->Index = Function->Top - 1;
	Child = Child->Next;
	mlc_decl_t *NextDecl = Decl->Next;
	Decl->Next = Function->Decls;
	Function->Decls = Decl;
	while (NextDecl) {
		Decl = NextDecl;
		NextDecl = Decl->Next;
		mlc_compiled_t ChildCompiled = ml_compile(Function, Child, HashContext);
		ml_connect(Compiled.Exits, ChildCompiled.Start);
		Compiled.Exits = ChildCompiled.Exits;
		Decl->Index = Function->Top - 1;
		Decl->Next = Function->Decls;
		Function->Decls = Decl;
		Child = Child->Next;
	}
	mlc_compiled_t ChildCompiled = ml_compile(Function, Child, HashContext);
	ml_connect(Compiled.Exits, ChildCompiled.Start);
	ML_COMPILE_HASH
	ml_inst_t *ExitInst = ml_inst_new(2, Expr->Source);
	ExitInst->run = mli_exit_run;
	ExitInst->Params[1].Count = Function->Top - OldTop;
	ml_connect(ChildCompiled.Exits, ExitInst);
	Compiled.Exits = ExitInst;
	Function->Decls = OldScope;
	Function->Top = OldTop;
	return Compiled;
}

static mlc_compiled_t ml_for_expr_compile(mlc_function_t *Function, mlc_decl_expr_t *Expr, SHA256_CTX *HashContext) {
	int OldTop = Function->Top + 1;
	mlc_decl_t *OldScope = Function->Decls;
	mlc_expr_t *Child = Expr->Child;
	mlc_compiled_t Compiled = ml_compile(Function, Child, HashContext);
	ml_inst_t *StartInst = ml_inst_new(2, Expr->Source);
	StartInst->run = mli_start_run;
	ml_connect(Compiled.Exits, StartInst);
	mlc_decl_t *Decl = Expr->Decl;
	Decl->Index = Function->Top - 1;
	mlc_decl_t *KeyDecl = Decl->Next;
	if (KeyDecl) {
		KeyDecl->Index = Function->Top;
		if (++Function->Top >= Function->Size) Function->Size = Function->Top + 1;
		KeyDecl->Next = Function->Decls;
	} else {
		Decl->Next = Function->Decls;
	}
	Function->Decls = Decl;
	ML_COMPILE_HASH
	ml_inst_t *ExitInst = ml_inst_new(2, Expr->Source);
	ExitInst->run = mli_exit_run;
	ExitInst->Params[0].Inst = StartInst;
	ExitInst->Params[1].Count = 1;
	ML_COMPILE_HASH
	ml_inst_t *NextInst = ml_inst_new(2, Expr->Source);
	NextInst->run = mli_next_run;
	StartInst->Params[0].Inst = NextInst;
	mlc_loop_t Loop = {Function->Loop, {NextInst, ExitInst}, Function->Top + 1};
	mlc_compiled_t BodyCompiled = ml_compile(Function, Child->Next, HashContext);
	ML_COMPILE_HASH
	ml_inst_t *PopInst = ml_inst_new(1, Expr->Source);
	PopInst->run = mli_pop_run;
	ml_connect(BodyCompiled.Exits, PopInst);
	if (KeyDecl) {
		ml_inst_t *KeyInst = ml_inst_new(1, Expr->Source);
		KeyInst->run = mli_key_run;
		KeyInst->Params[0].Inst = BodyCompiled.Start;
		NextInst->Params[1].Inst = KeyInst;
		StartInst->Params[1].Inst = KeyInst;
		ml_inst_t *PopInst2 = ml_inst_new(1, Expr->Source);
		PopInst2->run = mli_pop_run;
		PopInst->Params[0].Inst = PopInst2;
		PopInst2->Params[0].Inst = NextInst;
	} else {
		PopInst->Params[0].Inst = NextInst;
		NextInst->Params[1].Inst = BodyCompiled.Start;
		StartInst->Params[1].Inst = BodyCompiled.Start;
	}
	Compiled.Exits = ExitInst;
	Function->Loop = Loop.Up;
	Function->Top = OldTop;
	return Compiled;
}

static mlc_compiled_t ml_block_expr_compile(mlc_function_t *Function, mlc_decl_expr_t *Expr, SHA256_CTX *HashContext) {
	int OldTop = Function->Top + 1, NumDecls = 0;
	mlc_decl_t *OldScope = Function->Decls;
	for (mlc_decl_t *Decl = Expr->Decl; Decl;) {
		Decl->Index = Function->Top++;
		mlc_decl_t *NextDecl = Decl->Next;
		Decl->Next = Function->Decls;
		Function->Decls = Decl;
		Decl = NextDecl;
		++NumDecls;
	}
	if (Function->Top >= Function->Size) Function->Size = Function->Top + 1;
	mlc_expr_t *Child = Expr->Child;
	mlc_compiled_t Compiled = ml_compile(Function, Child, HashContext);
	if (Child) while ((Child = Child->Next)) {
		ML_COMPILE_HASH
		ml_inst_t *PopInst = ml_inst_new(1, Expr->Source);
		PopInst->run = mli_pop_run;
		ml_connect(Compiled.Exits, PopInst);
		--Function->Top;
		mlc_compiled_t ChildCompiled = ml_compile(Function, Child, HashContext);
		PopInst->Params[0].Inst = ChildCompiled.Start;
		Compiled.Exits = ChildCompiled.Exits;
	}
	if (NumDecls > 0) {
		ML_COMPILE_HASH
		ml_inst_t *EnterInst = ml_inst_new(2, Expr->Source);
		EnterInst->run = mli_enter_run;
		EnterInst->Params[0].Inst = Compiled.Start;
		EnterInst->Params[1].Count = NumDecls;
		Compiled.Start = EnterInst;
	}
	if (Function->Top > OldTop) {
		ml_inst_t *ExitInst = ml_inst_new(2, Expr->Source);
		ExitInst->run = mli_exit_run;
		ExitInst->Params[1].Count = Function->Top - OldTop;
		ml_connect(Compiled.Exits, ExitInst);
		Compiled.Exits = ExitInst;
		Function->Top -= NumDecls;
	}
	Function->Decls = OldScope;
	Function->Top = OldTop;
	return Compiled;
}

static mlc_compiled_t ml_call_expr_compile(mlc_function_t *Function, mlc_parent_expr_t *Expr, SHA256_CTX *HashContext) {
	mlc_compiled_t Compiled = ml_compile(Function, Expr->Child, HashContext);
	int NumArgs = 0;
	for (mlc_expr_t *Child = Expr->Child->Next; Child; Child = Child->Next) {
		++NumArgs;
		mlc_compiled_t ChildCompiled = ml_compile(Function, Child, HashContext);
		ml_connect(Compiled.Exits, ChildCompiled.Start);
		Compiled.Exits = ChildCompiled.Exits;
	}
	ML_COMPILE_HASH
	ml_inst_t *CallInst = ml_inst_new(2, Expr->Source);
	CallInst->run = mli_call_run;
	CallInst->Params[1].Count = NumArgs;
	ml_connect(Compiled.Exits, CallInst);
	Compiled.Exits = CallInst;
	Function->Top -= NumArgs;
	return Compiled;
}

static mlc_compiled_t ml_index_expr_compile(mlc_function_t *Function, mlc_parent_expr_t *Expr, SHA256_CTX *HashContext) {
	mlc_compiled_t Compiled = ml_compile(Function, Expr->Child, HashContext);
	int NumArgs = 0;
	for (mlc_expr_t *Child = Expr->Child->Next; Child; Child = Child->Next) {
		++NumArgs;
		mlc_compiled_t ChildCompiled = ml_compile(Function, Child, HashContext);
		ml_connect(Compiled.Exits, ChildCompiled.Start);
		Compiled.Exits = ChildCompiled.Exits;
	}
	ML_COMPILE_HASH
	ml_inst_t *CallInst = ml_inst_new(2, Expr->Source);
	CallInst->run = mli_index_run;
	CallInst->Params[1].Count = NumArgs;
	ml_connect(Compiled.Exits, CallInst);
	Compiled.Exits = CallInst;
	Function->Top -= NumArgs;
	return Compiled;
}

static mlc_compiled_t ml_assign_expr_compile(mlc_function_t *Function, mlc_parent_expr_t *Expr, SHA256_CTX *HashContext) {
	int OldSelf = Function->Self;
	mlc_compiled_t Compiled = ml_compile(Function, Expr->Child, HashContext);
	Function->Self = Function->Top - 1;
	mlc_compiled_t ValueCompiled = ml_compile(Function, Expr->Child->Next, HashContext);
	ml_connect(Compiled.Exits, ValueCompiled.Start);
	ML_COMPILE_HASH
	ml_inst_t *AssignInst = ml_inst_new(1, Expr->Source);
	AssignInst->run = mli_assign_run;
	ml_connect(ValueCompiled.Exits, AssignInst);
	Compiled.Exits = AssignInst;
	Function->Top -= 1;
	Function->Self = OldSelf;
	return Compiled;
}

static mlc_compiled_t ml_self_expr_compile(mlc_function_t *Function, mlc_expr_t *Expr, SHA256_CTX *HashContext) {
	ML_COMPILE_HASH
	ml_inst_t *SelfInst = ml_inst_new(2, Expr->Source);
	SelfInst->run = mli_local_run;
	SelfInst->Params[1].Index = Function->Self;
	if (++Function->Top >= Function->Size) Function->Size = Function->Top + 1;
	return (mlc_compiled_t){SelfInst, SelfInst};
}


struct mlc_const_call_expr_t {
	MLC_EXPR_FIELDS(const_call);
	mlc_expr_t *Child;
	ml_value_t *Value;
};

static mlc_compiled_t ml_const_call_expr_compile(mlc_function_t *Function, mlc_const_call_expr_t *Expr, SHA256_CTX *HashContext) {
	int OldTop = Function->Top + 1;
	if (OldTop >= Function->Size) Function->Size = Function->Top + 1;
	long ValueHash = ml_hash(Function->ML, Expr->Value);
	sha256_update(HashContext, (void *)&ValueHash, sizeof(ValueHash));
	ML_COMPILE_HASH
	ml_inst_t *CallInst = ml_inst_new(2, Expr->Source);
	CallInst->run = mli_const_call_run;
	CallInst->Params[2].Value = Expr->Value;
	if (Expr->Child) {
		int NumArgs = 1;
		mlc_compiled_t Compiled = ml_compile(Function, Expr->Child, HashContext);
		for (mlc_expr_t *Child = Expr->Child->Next; Child; Child = Child->Next) {
			++NumArgs;
			mlc_compiled_t ChildCompiled = ml_compile(Function, Child, HashContext);
			ml_connect(Compiled.Exits, ChildCompiled.Start);
			Compiled.Exits = ChildCompiled.Exits;
		}
		CallInst->Params[1].Count = NumArgs;
		ml_connect(Compiled.Exits, CallInst);
		Compiled.Exits = CallInst;
		Function->Top = OldTop;
		return Compiled;
	} else {
		CallInst->Params[1].Count = 0;
		Function->Top = OldTop;
		return (mlc_compiled_t){CallInst, CallInst};
	}
}

struct mlc_fun_expr_t {
	MLC_EXPR_FIELDS(fun);
	mlc_decl_t *Params;
	mlc_expr_t *Body;
};

static mlc_compiled_t ml_fun_expr_compile(mlc_function_t *Function, mlc_fun_expr_t *Expr, SHA256_CTX *HashContext) {
	// closure <entry> <frame_size> <num_params> <num_upvalues> <upvalue_1> ...
	mlc_function_t SubFunction[1] = {0,};
	SubFunction->ML = Function->ML;
	SubFunction->Up = Function;
	int NumParams = 0;
	mlc_decl_t **ParamSlot = &SubFunction->Decls;
	for (mlc_decl_t *Param = Expr->Params; Param;) {
		mlc_decl_t *NextParam = Param->Next;
		++NumParams;
		if (Param->Index) NumParams = ~NumParams;
		Param->Index = SubFunction->Top++;
		ParamSlot[0] = Param;
		ParamSlot = &Param->Next;
		Param = NextParam;
	}
	SubFunction->Size = SubFunction->Top + 1;
	SHA256_CTX SubHashContext[1];
	sha256_init(SubHashContext);
	mlc_compiled_t Compiled = ml_compile(SubFunction, Expr->Body, SubHashContext);
	if (Compiled.Exits) ml_connect(Compiled.Exits, 0);
	int NumUpValues = 0;
	for (mlc_upvalue_t *UpValue = SubFunction->UpValues; UpValue; UpValue = UpValue->Next) ++NumUpValues;
	ML_COMPILE_HASH
	ml_inst_t *ClosureInst = ml_inst_new(3 + NumUpValues, Expr->Source);
	ClosureInst->run = mli_closure_run;
	ml_param_t *Params = ClosureInst->Params;
	ml_closure_info_t *Info = new(ml_closure_info_t);
	Info->Entry = Compiled.Start;
	Info->FrameSize = SubFunction->Size;
	Info->NumParams = NumParams;
	sha256_final(SubHashContext, Info->Hash);
	Params[1].ClosureInfo = Info;
	Params[2].Count = NumUpValues;
	int Index = 3;
	for (mlc_upvalue_t *UpValue = SubFunction->UpValues; UpValue; UpValue = UpValue->Next) Params[Index++].Index = UpValue->Index;
	if (++Function->Top >= Function->Size) Function->Size = Function->Top + 1;
	return (mlc_compiled_t){ClosureInst, ClosureInst};
}

struct mlc_ident_expr_t {
	MLC_EXPR_FIELDS(ident);
	const char *Ident;
};

static int ml_upvalue_find(mlc_function_t *Function, mlc_decl_t *Decl, mlc_function_t *Origin) {
	if (Function == Origin) return Decl->Index;
	mlc_upvalue_t **UpValueSlot = &Function->UpValues;
	int Index = 0;
	while (UpValueSlot[0]) {
		if (UpValueSlot[0]->Decl == Decl) return Index;
		UpValueSlot = &UpValueSlot[0]->Next;
		++Index;
	}
	mlc_upvalue_t *UpValue = new(mlc_upvalue_t);
	UpValue->Decl = Decl;
	UpValue->Index = ml_upvalue_find(Function->Up, Decl, Origin);
	UpValueSlot[0] = UpValue;
	return ~Index;
}

static mlc_compiled_t ml_ident_expr_compile(mlc_function_t *Function, mlc_ident_expr_t *Expr, SHA256_CTX *HashContext) {
	for (mlc_function_t *UpFunction = Function; UpFunction; UpFunction = UpFunction->Up) {
		for (mlc_decl_t *Decl = UpFunction->Decls; Decl; Decl = Decl->Next) {
			if (!strcmp(Decl->Ident, Expr->Ident)) {
				int Index = ml_upvalue_find(Function, Decl, UpFunction);
				sha256_update(HashContext, (void *)&Index, sizeof(Index));
				ML_COMPILE_HASH
				ml_inst_t *LocalInst = ml_inst_new(2, Expr->Source);
				LocalInst->run = mli_local_run;
				LocalInst->Params[1].Index = Index;
				if (++Function->Top >= Function->Size) Function->Size = Function->Top + 1;
				return (mlc_compiled_t){LocalInst, LocalInst};
			}
		}
	}
	sha256_update(HashContext, Expr->Ident, strlen(Expr->Ident));
	ML_COMPILE_HASH
	ml_inst_t *ValueInst = ml_inst_new(2, Expr->Source);
	ValueInst->run = mli_push_run;
	ValueInst->Params[1].Value = (Function->ML->Get)(Function->ML, Function->ML->Data, Expr->Ident);
	if (++Function->Top >= Function->Size) Function->Size = Function->Top + 1;
	return (mlc_compiled_t){ValueInst, ValueInst};
}

struct mlc_value_expr_t {
	MLC_EXPR_FIELDS(value);
	ml_value_t *Value;
};

static mlc_compiled_t ml_value_expr_compile(mlc_function_t *Function, mlc_value_expr_t *Expr, SHA256_CTX *HashContext) {
	long ValueHash = ml_hash(Function->ML, Expr->Value);
	sha256_update(HashContext, (void *)&ValueHash, sizeof(ValueHash));
	ML_COMPILE_HASH
	ml_inst_t *ValueInst = ml_inst_new(2, Expr->Source);
	ValueInst->run = mli_push_run;
	ValueInst->Params[1].Value = Expr->Value;
	if (++Function->Top >= Function->Size) Function->Size = Function->Top + 1;
	return (mlc_compiled_t){ValueInst, ValueInst};
}

typedef enum ml_token_t {
	MLT_NONE,
	MLT_EOL,
	MLT_EOI,
	MLT_IF,
	MLT_THEN,
	MLT_ELSEIF,
	MLT_ELSE,
	MLT_END,
	MLT_LOOP,
	MLT_WHILE,
	MLT_UNTIL,
	MLT_EXIT,
	MLT_NEXT,
	MLT_FOR,
	MLT_IN,
	MLT_IS,
	MLT_FUN,
	MLT_RETURN,
	MLT_WITH,
	MLT_DO,
	MLT_NIL,
	MLT_VAR,
	MLT_IDENT,
	MLT_LEFT_PAREN,
	MLT_RIGHT_PAREN,
	MLT_LEFT_SQUARE,
	MLT_RIGHT_SQUARE,
	MLT_LEFT_BRACE,
	MLT_RIGHT_BRACE,
	MLT_DOT,
	MLT_SELF,
	MLT_SEMICOLON,
	MLT_COMMA,
	MLT_ASSIGN,
	MLT_VALUE,
	MLT_EXPR,
	MLT_OPERATOR
} ml_token_t;

const char *MLTokens[] = {
	"", // MLT_NONE,
	"<end of line>", // MLT_EOL,
	"<end of input>", // MLT_EOI,
	"if", // MLT_IF,
	"then", // MLT_THEN,
	"elseif", // MLT_ELSEIF,
	"else", // MLT_ELSE,
	"end", // MLT_END,
	"loop", // MLT_LOOP,
	"while", // MLT_WHILE,
	"until", // MLT_UNTIL,
	"exit", // MLT_EXIT,
	"next", // MLT_NEXT,
	"for", // MLT_FOR,
	"in", // MLT_IN,
	"is", // MLT_IS,
	"fun", // MLT_FUN,
	"return", // MLT_RETURN,
	"with", // MLT_WITH,
	"do", // MLT_DO,
	"nil", // MLT_NIL,
	"var", // MLT_VAR,
	"<identifier>", // MLT_IDENT,
	"(", // MLT_LEFT_PAREN,
	")", // MLT_RIGHT_PAREN,
	"[", // MLT_LEFT_SQUARE,
	"]", // MLT_RIGHT_SQUARE,
	"{", // MLT_LEFT_BRACE,
	"}", // MLT_RIGHT_BRACE,
	".", // MLT_DOT,
	"$", // MLT_SELF,
	";", // MLT_SEMICOLON,
	",", // MLT_COMMA,
	":=", // MLT_ASSIGN,
	"<value>", // MLT_VALUE,
	"<expr>", // MLT_EXPR,
	"<operator>", // MLT_OPERATOR
};

struct mlc_scanner_t {
	ml_t *ML;
	const char *Next;
	ml_source_t Source;
	ml_token_t Token;
	ml_value_t *Value;
	mlc_expr_t *Expr;
	const char *Ident;
	void *Data;
	const char *(*read)(void *);
} *ml_scanner(ml_t *ML, const char *SourceName, void *Data, const char *(*read)(void *)) {
	mlc_scanner_t *Scanner = new(mlc_scanner_t);
	Scanner->ML = ML;
	Scanner->Token = MLT_NONE;
	Scanner->Next = "";
	Scanner->Source.Name = SourceName;
	Scanner->Source.Line = 0;
	Scanner->Data = Data;
	Scanner->read = read;
	return Scanner;
}

static mlc_expr_t *ml_accept_term(mlc_scanner_t *Scanner);
static mlc_expr_t *ml_parse_expression(mlc_scanner_t *Scanner);
static mlc_expr_t *ml_accept_expression(mlc_scanner_t *Scanner);
static mlc_expr_t *ml_accept_block(mlc_scanner_t *Scanner);
static void ml_accept(mlc_scanner_t *Scanner, ml_token_t Token);

static mlc_expr_t *ml_accept_string(mlc_scanner_t *Scanner) {
	char Char = Scanner->Next[0];
	if (Char == '\'') {
		++Scanner->Next;
		return 0;
	}
	int Length = 0;
	const char *End = Scanner->Next;
	while (End[0] != '\'' && End[0] != '{') {
		if (!End[0]) {
			Scanner->ML->Error = ml_error("ParseError", "end of line while parsing string");
			ml_error_trace_add(Scanner->ML->Error, Scanner->Source);
			longjmp(Scanner->ML->OnError, 1);
		}
		if (End[0] == '\\') ++End;
		++Length;
		++End;
	}
	mlc_expr_t *Expr;
	if (Length > 0) {
		char *String = snew(Length + 1), *D = String;
		for (const char *S = Scanner->Next; S < End; ++S) {
			if (*S == '\\') {
				++S;
				switch (*S) {
				case 'r': *D++ = '\r'; break;
				case 'n': *D++ = '\n'; break;
				case 't': *D++ = '\t'; break;
				case '\'': *D++ = '\''; break;
				case '\"': *D++ = '\"'; break;
				case '\\': *D++ = '\\'; break;
				}
			} else {
				*D++ = *S;
			}
		}
		*D = 0;
		mlc_value_expr_t *ValueExpr = new(mlc_value_expr_t);
		ValueExpr->compile = ml_value_expr_compile;
		ValueExpr->Source = Scanner->Source;
		ValueExpr->Source = Scanner->Source;
		ValueExpr->Value = ml_string(String, Length);
		Expr = (mlc_expr_t *)ValueExpr;
	}
	Scanner->Next = End + 1;
	if (End[0] == '{') {
		mlc_expr_t *Embedded = ml_accept_expression(Scanner);
		ml_accept(Scanner, MLT_RIGHT_BRACE);
		Embedded->Next = ml_accept_string(Scanner);
		if (Expr) {
			Expr->Next = Embedded;
		} else {
			Expr = Embedded;
		}
	}
	return Expr;
}

static int ml_parse(mlc_scanner_t *Scanner, ml_token_t Token) {
	if (Scanner->Token == MLT_NONE) for (;;) {
		char Char = Scanner->Next[0];
		if (!Char) {
			Scanner->Next = (Scanner->read)(Scanner->Data);
			++Scanner->Source.Line;
			if (Scanner->Next) continue;
			Scanner->Token = MLT_EOI;
			goto done;
		}
		if (Char == '\n') {
			++Scanner->Next;
			Scanner->Token = MLT_EOL;
			goto done;
		}
		if (Char <= ' ') {
			++Scanner->Next;
			continue;
		}
		if (isalpha(Char) || Char == '_') {
			const char *End = Scanner->Next;
			for (Char = End[0]; isalnum(Char) || Char == '_'; Char = *++End);
			int Length = End - Scanner->Next;
			for (ml_token_t T = MLT_IF; T <= MLT_VAR; ++T) {
				const char *P = Scanner->Next;
				const char *C = MLTokens[T];
				while (*C && *++C == *++P);
				if (!*C && P == End) {
					Scanner->Token = T;
					Scanner->Next = End;
					goto done;
				}
			}
			char *Ident = snew(Length + 1);
			memcpy(Ident, Scanner->Next, Length);
			Ident[Length] = 0;
			Scanner->Ident = Ident;
			Scanner->Token = MLT_IDENT;
			Scanner->Next = End;
			goto done;
		}
		if (isdigit(Char) || (Char == '-' && isdigit(Scanner->Next[1]))) {
			char *End;
			double Double = strtod(Scanner->Next, &End);
			for (const char *P = Scanner->Next; P < End; ++P) {
				if (P[0] == '.' || P[0] == 'e' || P[0] == 'E') {
					Scanner->Value = ml_real(Double);
					Scanner->Token = MLT_VALUE;
					Scanner->Next = End;
					goto done;
				}
			}
			long Integer = strtol(Scanner->Next, &End, 10);
			Scanner->Value = ml_integer(Integer);
			Scanner->Token = MLT_VALUE;
			Scanner->Next = End;
			goto done;
		}
		if (Char == '\'') {
			++Scanner->Next;
			mlc_const_call_expr_t *CallExpr = new(mlc_const_call_expr_t);
			CallExpr->compile = ml_const_call_expr_compile;
			CallExpr->Source = Scanner->Source;
			CallExpr->Value = (ml_value_t *)StringNew;
			CallExpr->Child = ml_accept_string(Scanner);
			Scanner->Token = MLT_EXPR;
			Scanner->Expr = (mlc_expr_t *)CallExpr;
			goto done;
		}
		if (Char == '\"') {
			++Scanner->Next;
			int Length = 0;
			const char *End = Scanner->Next;
			while (End[0] != '\"') {
				if (!End[0]) {
					Scanner->ML->Error = ml_error("ParseError", "end of input while parsing string");
					ml_error_trace_add(Scanner->ML->Error, Scanner->Source);
					longjmp(Scanner->ML->OnError, 1);
				}
				if (End[0] == '\\') ++End;
				++Length;
				++End;
			}
			char *String = snew(Length + 1), *D = String;
			for (const char *S = Scanner->Next; S < End; ++S) {
				if (*S == '\\') {
					++S;
					switch (*S) {
					case 'r': *D++ = '\r'; break;
					case 'n': *D++ = '\n'; break;
					case 't': *D++ = '\t'; break;
					case '\'': *D++ = '\''; break;
					case '\"': *D++ = '\"'; break;
					case '\\': *D++ = '\\'; break;
					}
				} else {
					*D++ = *S;
				}
			}
			*D = 0;
			Scanner->Value = ml_string(String, Length);
			Scanner->Token = MLT_VALUE;
			Scanner->Next = End + 1;
			goto done;
		}
		if (Char == ':' && Scanner->Next[1] == '=') {
			Scanner->Token = MLT_ASSIGN;
			Scanner->Next += 2;
			goto done;
		}
		if (Char == '-' && Scanner->Next[1] == '-') {
			Scanner->Next = "";
			continue;
		}
		for (ml_token_t T = MLT_LEFT_PAREN; T <= MLT_COMMA; ++T) {
			if (Char == MLTokens[T][0]) {
				Scanner->Token = T;
				++Scanner->Next;
				goto done;
			}
		}
		if (isgraph(Char)) {
			const char *End = Scanner->Next;
			for (Char = End[0]; !isalnum(Char) && isgraph(Char); Char = *++End);
			int Length = End - Scanner->Next;
			char *Operator = snew(Length + 1);
			strncpy(Operator, Scanner->Next, Length);
			Operator[Length] = 0;
			Scanner->Ident = Operator;
			Scanner->Token = MLT_OPERATOR;
			Scanner->Next = End;
			goto done;
		}
		Scanner->ML->Error = ml_error("ParseError", "unexpected character <%c>", Char);
		ml_error_trace_add(Scanner->ML->Error, Scanner->Source);
		longjmp(Scanner->ML->OnError, 1);
	}
	done:
	if (Scanner->Token == Token) {
		Scanner->Token = MLT_NONE;
		return 1;
	} else {
		return 0;
	}
}

static void ml_accept(mlc_scanner_t *Scanner, ml_token_t Token) {
	while (ml_parse(Scanner, MLT_EOL));
	if (ml_parse(Scanner, Token)) return;
	Scanner->ML->Error = ml_error("ParseError", "expected %s not %s", MLTokens[Token], MLTokens[Scanner->Token]);
	ml_error_trace_add(Scanner->ML->Error, Scanner->Source);
	longjmp(Scanner->ML->OnError, 1);
}

static const char *ml_file_read(void *Data) {
	FILE *File = (FILE *)Data;
	char *Line = 0;
	size_t Length;
	if (getline(&Line, &Length, File) < 0) return 0;
	return Line;
}

static mlc_expr_t *ml_parse_term(mlc_scanner_t *Scanner) {
	if (ml_parse(Scanner, MLT_IF)) {
		mlc_if_expr_t *IfExpr = new(mlc_if_expr_t);
		IfExpr->compile = ml_if_expr_compile;
		IfExpr->Source = Scanner->Source;
		mlc_if_case_t **CaseSlot = &IfExpr->Cases;
		do {
			mlc_if_case_t *Case = CaseSlot[0] = new(mlc_if_case_t);
			CaseSlot = &Case->Next;
			Case->Source = Scanner->Source;
			Case->Condition = ml_accept_expression(Scanner);
			ml_accept(Scanner, MLT_THEN);
			Case->Body = ml_accept_block(Scanner);
		} while (ml_parse(Scanner, MLT_ELSEIF));
		if (ml_parse(Scanner, MLT_ELSE)) IfExpr->Else = ml_accept_block(Scanner);
		ml_accept(Scanner, MLT_END);
		return (mlc_expr_t *)IfExpr;
	} else if (ml_parse(Scanner, MLT_LOOP)) {
		mlc_parent_expr_t *LoopExpr = new(mlc_parent_expr_t);
		LoopExpr->compile = ml_loop_expr_compile;
		LoopExpr->Source = Scanner->Source;
		LoopExpr->Child = ml_accept_block(Scanner);
		ml_accept(Scanner, MLT_END);
		return (mlc_expr_t *)LoopExpr;
	} else if (ml_parse(Scanner, MLT_FOR)) {
		mlc_decl_expr_t *ForExpr = new(mlc_decl_expr_t);
		ForExpr->compile = ml_for_expr_compile;
		ForExpr->Source = Scanner->Source;
		mlc_decl_t *Decl = new(mlc_decl_t);
		ml_accept(Scanner, MLT_IDENT);
		Decl->Ident = Scanner->Ident;
		if (ml_parse(Scanner, MLT_COMMA)) {
			ml_accept(Scanner, MLT_IDENT);
			mlc_decl_t *KeyDecl = new(mlc_decl_t);
			KeyDecl->Ident = Scanner->Ident;
			Decl->Next = KeyDecl;
		}
		ForExpr->Decl = Decl;
		if (ml_parse(Scanner, MLT_ASSIGN)) {
			ForExpr->Child = ml_accept_expression(Scanner);
		} else {
			ml_accept(Scanner, MLT_IN);
			mlc_const_call_expr_t *CallExpr = new(mlc_const_call_expr_t);
			CallExpr->compile = ml_const_call_expr_compile;
			CallExpr->Source = Scanner->Source;
			CallExpr->Value = ml_method("values");
			CallExpr->Child = ml_accept_expression(Scanner);
			ForExpr->Child = (mlc_expr_t *)CallExpr;
		}
		ml_accept(Scanner, MLT_DO);
		ForExpr->Child->Next = ml_accept_block(Scanner);
		ml_accept(Scanner, MLT_END);
		return (mlc_expr_t *)ForExpr;
	} else if (ml_parse(Scanner, MLT_WHILE)) {
		mlc_if_expr_t *WhileExpr = new(mlc_if_expr_t);
		WhileExpr->compile = ml_if_expr_compile;
		WhileExpr->Source = Scanner->Source;
		mlc_if_case_t *Case = new(mlc_if_case_t);
		Case->Source = Scanner->Source;
		Case->Condition = ml_accept_expression(Scanner);
		WhileExpr->Cases = Case;
		mlc_parent_expr_t *ExitExpr = new(mlc_parent_expr_t);
		ExitExpr->Source = Scanner->Source;
		WhileExpr->Else = (mlc_expr_t *)ExitExpr;
		return (mlc_expr_t *)WhileExpr;
	} else if (ml_parse(Scanner, MLT_UNTIL)) {
		mlc_if_expr_t *UntilExpr = new(mlc_if_expr_t);
		UntilExpr->compile = ml_if_expr_compile;
		UntilExpr->Source = Scanner->Source;
		mlc_if_case_t *Case = new(mlc_if_case_t);
		Case->Source = Scanner->Source;
		Case->Condition = ml_accept_expression(Scanner);
		UntilExpr->Cases = Case;
		mlc_parent_expr_t *ExitExpr = new(mlc_parent_expr_t);
		ExitExpr->compile = ml_exit_expr_compile;
		ExitExpr->Source = Scanner->Source;
		Case->Body = (mlc_expr_t *)ExitExpr;
		return (mlc_expr_t *)UntilExpr;
	} else if (ml_parse(Scanner, MLT_EXIT)) {
		mlc_parent_expr_t *ExitExpr = new(mlc_parent_expr_t);
		ExitExpr->compile = ml_exit_expr_compile;
		ExitExpr->Source = Scanner->Source;
		ExitExpr->Child = ml_parse_expression(Scanner);
		return (mlc_expr_t *)ExitExpr;
	} else if (ml_parse(Scanner, MLT_NEXT)) {
		mlc_expr_t *NextExpr = new(mlc_expr_t);
		NextExpr->compile = ml_next_expr_compile;
		NextExpr->Source = Scanner->Source;
		return NextExpr;
	} else if (ml_parse(Scanner, MLT_FUN)) {
		mlc_fun_expr_t *FunExpr = new(mlc_fun_expr_t);
		FunExpr->compile = ml_fun_expr_compile;
		FunExpr->Source = Scanner->Source;
		ml_accept(Scanner, MLT_LEFT_PAREN);
		if (!ml_parse(Scanner, MLT_RIGHT_PAREN)) {
			mlc_decl_t **ParamSlot = &FunExpr->Params;
			do {
				ml_accept(Scanner, MLT_IDENT);
				mlc_decl_t *Param = ParamSlot[0] = new(mlc_decl_t);
				ParamSlot = &Param->Next;
				Param->Ident = Scanner->Ident;
				if (ml_parse(Scanner, MLT_SELF)) {
					Param->Index = 1;
					break;
				}
			} while (ml_parse(Scanner, MLT_COMMA));
			ml_accept(Scanner, MLT_RIGHT_PAREN);
		}
		FunExpr->Body = ml_accept_block(Scanner);
		ml_accept(Scanner, MLT_END);
		return (mlc_expr_t *)FunExpr;
	} else if (ml_parse(Scanner, MLT_RETURN)) {
		mlc_parent_expr_t *ReturnExpr = new(mlc_parent_expr_t);
		ReturnExpr->compile = ml_return_expr_compile;
		ReturnExpr->Source = Scanner->Source;
		ReturnExpr->Child = ml_parse_expression(Scanner);
		return (mlc_expr_t *)ReturnExpr;
	} else if (ml_parse(Scanner, MLT_WITH)) {
		mlc_decl_expr_t *WithExpr = new(mlc_decl_expr_t);
		WithExpr->compile = ml_with_expr_compile;
		WithExpr->Source = Scanner->Source;
		mlc_decl_t **DeclSlot = &WithExpr->Decl;
		mlc_expr_t **ExprSlot = &WithExpr->Child;
		do {
			ml_accept(Scanner, MLT_IDENT);
			mlc_decl_t *Decl = DeclSlot[0] = new(mlc_decl_t);
			DeclSlot = &Decl->Next;
			Decl->Ident = Scanner->Ident;
			ml_accept(Scanner, MLT_ASSIGN);
			mlc_expr_t *Expr = ExprSlot[0] = ml_accept_expression(Scanner);
			ExprSlot = &Expr->Next;
		} while (ml_parse(Scanner, MLT_COMMA));
		ml_accept(Scanner, MLT_DO);
		ExprSlot[0] = ml_accept_block(Scanner);
		ml_accept(Scanner, MLT_END);
		return (mlc_expr_t *)WithExpr;
	} else if (ml_parse(Scanner, MLT_IDENT)) {
		mlc_ident_expr_t *IdentExpr = new(mlc_ident_expr_t);
		IdentExpr->compile = ml_ident_expr_compile;
		IdentExpr->Source = Scanner->Source;
		IdentExpr->Ident = Scanner->Ident;
		return (mlc_expr_t *)IdentExpr;
	} else if (ml_parse(Scanner, MLT_VALUE)) {
		mlc_value_expr_t *ValueExpr = new(mlc_value_expr_t);
		ValueExpr->compile = ml_value_expr_compile;
		ValueExpr->Source = Scanner->Source;
		ValueExpr->Value = Scanner->Value;
		return (mlc_expr_t *)ValueExpr;
	} else if (ml_parse(Scanner, MLT_EXPR)) {
		return Scanner->Expr;
	} else if (ml_parse(Scanner, MLT_NIL)) {
		mlc_value_expr_t *ValueExpr = new(mlc_value_expr_t);
		ValueExpr->compile = ml_value_expr_compile;
		ValueExpr->Source = Scanner->Source;
		ValueExpr->Value = Nil;
		return (mlc_expr_t *)ValueExpr;
	} else if (ml_parse(Scanner, MLT_LEFT_PAREN)) {
		mlc_expr_t *Expr = ml_accept_expression(Scanner);
		ml_accept(Scanner, MLT_RIGHT_PAREN);
		return Expr;
	} else if (ml_parse(Scanner, MLT_LEFT_SQUARE)) {
		mlc_const_call_expr_t *CallExpr = new(mlc_const_call_expr_t);
		CallExpr->compile = ml_const_call_expr_compile;
		CallExpr->Source = Scanner->Source;
		CallExpr->Value = (ml_value_t *)ListNew;
		mlc_expr_t **ArgsSlot = &CallExpr->Child;
		if (!ml_parse(Scanner, MLT_RIGHT_SQUARE)) {
			do {
				mlc_expr_t *Arg = ArgsSlot[0] = ml_accept_expression(Scanner);
				ArgsSlot = &Arg->Next;
			} while (ml_parse(Scanner, MLT_COMMA));
			ml_accept(Scanner, MLT_RIGHT_SQUARE);
		}
		return (mlc_expr_t *)CallExpr;
	} else if (ml_parse(Scanner, MLT_LEFT_BRACE)) {
		mlc_const_call_expr_t *CallExpr = new(mlc_const_call_expr_t);
		CallExpr->compile = ml_const_call_expr_compile;
		CallExpr->Source = Scanner->Source;
		CallExpr->Value = (ml_value_t *)TreeNew;
		mlc_expr_t **ArgsSlot = &CallExpr->Child;
		if (!ml_parse(Scanner, MLT_RIGHT_BRACE)) {
			do {
				mlc_expr_t *Arg = ArgsSlot[0] = ml_accept_expression(Scanner);
				ArgsSlot = &Arg->Next;
				if (ml_parse(Scanner, MLT_IS)) {
					mlc_expr_t *ArgExpr = ArgsSlot[0] = ml_accept_expression(Scanner);
					ArgsSlot = &ArgExpr->Next;
				} else {
					mlc_value_expr_t *ArgExpr = new(mlc_value_expr_t);
					ArgExpr->compile = ml_value_expr_compile;
					ArgExpr->Source = Scanner->Source;
					ArgExpr->Value = Nil;
					ArgsSlot[0] = (mlc_expr_t *)ArgExpr;
					ArgsSlot = &Arg->Next;
				}
			} while (ml_parse(Scanner, MLT_COMMA));
			ml_accept(Scanner, MLT_RIGHT_BRACE);
		}
		return (mlc_expr_t *)CallExpr;
	} else if (ml_parse(Scanner, MLT_SELF)) {
		mlc_expr_t *SelfExpr = new(mlc_expr_t);
		SelfExpr->compile = ml_self_expr_compile;
		SelfExpr->Source = Scanner->Source;
		return SelfExpr;
	} else if (ml_parse(Scanner, MLT_OPERATOR)) {
		mlc_const_call_expr_t *CallExpr = new(mlc_const_call_expr_t);
		CallExpr->compile = ml_const_call_expr_compile;
		CallExpr->Source = Scanner->Source;
		CallExpr->Value = (ml_value_t *)ml_method(Scanner->Ident);
		CallExpr->Child = ml_accept_term(Scanner);
		return (mlc_expr_t *)CallExpr;
	} else if (ml_parse(Scanner, MLT_DOT)) {
		if (!ml_parse(Scanner, MLT_IDENT)) ml_accept(Scanner, MLT_OPERATOR);
		mlc_value_expr_t *ValueExpr = new(mlc_value_expr_t);
		ValueExpr->compile = ml_value_expr_compile;
		ValueExpr->Source = Scanner->Source;
		ValueExpr->Value = (ml_value_t *)ml_method(Scanner->Ident);
		return (mlc_expr_t *)ValueExpr;
	}
	return 0;
}

static mlc_expr_t *ml_accept_term(mlc_scanner_t *Scanner) {
	while (ml_parse(Scanner, MLT_EOL));
	mlc_expr_t *Expr = ml_parse_term(Scanner);
	if (Expr) return Expr;
	Scanner->ML->Error = ml_error("ParseError", "expected <term> not %s", MLTokens[Scanner->Token]);
	ml_error_trace_add(Scanner->ML->Error, Scanner->Source);
	longjmp(Scanner->ML->OnError, 1);
}

static mlc_expr_t *ml_parse_factor(mlc_scanner_t *Scanner) {
	mlc_expr_t *Expr = ml_parse_term(Scanner);
	if (!Expr) return 0;
	for (;;) {
		if (ml_parse(Scanner, MLT_LEFT_PAREN)) {
			mlc_parent_expr_t *CallExpr = new(mlc_parent_expr_t);
			CallExpr->compile = ml_call_expr_compile;
			CallExpr->Source = Scanner->Source;
			CallExpr->Child = Expr;
			mlc_expr_t **ArgsSlot = &Expr->Next;
			if (!ml_parse(Scanner, MLT_RIGHT_PAREN)) {
				do {
					mlc_expr_t *Arg = ArgsSlot[0] = ml_accept_expression(Scanner);
					ArgsSlot = &Arg->Next;
				} while (ml_parse(Scanner, MLT_COMMA));
				ml_accept(Scanner, MLT_RIGHT_PAREN);
			}
			Expr = (mlc_expr_t *)CallExpr;
		} else if (ml_parse(Scanner, MLT_LEFT_SQUARE)) {
			mlc_parent_expr_t *IndexExpr = new(mlc_parent_expr_t);
			IndexExpr->compile = ml_index_expr_compile;
			IndexExpr->Source = Scanner->Source;
			IndexExpr->Child = Expr;
			mlc_expr_t **ArgsSlot = &Expr->Next;
			if (!ml_parse(Scanner, MLT_RIGHT_SQUARE)) {
				do {
					mlc_expr_t *Arg = ArgsSlot[0] = ml_accept_expression(Scanner);
					ArgsSlot = &Arg->Next;
				} while (ml_parse(Scanner, MLT_COMMA));
				ml_accept(Scanner, MLT_RIGHT_SQUARE);
			}
			Expr = (mlc_expr_t *)IndexExpr;
		} else if (ml_parse(Scanner, MLT_DOT)) {
			mlc_const_call_expr_t *CallExpr = new(mlc_const_call_expr_t);
			CallExpr->compile = ml_const_call_expr_compile;
			CallExpr->Source = Scanner->Source;
			ml_accept(Scanner, MLT_IDENT);
			CallExpr->Value = (ml_value_t *)ml_method(Scanner->Ident);
			CallExpr->Child = Expr;
			if (ml_parse(Scanner, MLT_LEFT_PAREN) && !ml_parse(Scanner, MLT_RIGHT_PAREN)) {
				mlc_expr_t **ArgsSlot = &Expr->Next;
				if (!ml_parse(Scanner, MLT_RIGHT_PAREN)) {
					do {
						mlc_expr_t *Arg = ArgsSlot[0] = ml_accept_expression(Scanner);
						ArgsSlot = &Arg->Next;
					} while (ml_parse(Scanner, MLT_COMMA));
					ml_accept(Scanner, MLT_RIGHT_PAREN);
				}
			}
			Expr = (mlc_expr_t *)CallExpr;
		} else {
			return Expr;
		}
	}
	return 0; // Unreachable
}

static mlc_expr_t *ml_accept_factor(mlc_scanner_t *Scanner) {
	while (ml_parse(Scanner, MLT_EOL));
	mlc_expr_t *Expr = ml_parse_factor(Scanner);
	if (Expr) return Expr;
	Scanner->ML->Error = ml_error("ParseError", "expected <factor> not %s", MLTokens[Scanner->Token]);
	ml_error_trace_add(Scanner->ML->Error, Scanner->Source);
	longjmp(Scanner->ML->OnError, 1);
}

static mlc_expr_t *ml_parse_expression(mlc_scanner_t *Scanner) {
	mlc_expr_t *Expr = ml_parse_factor(Scanner);
	if (!Expr) return 0;
	for (;;) {
		if (ml_parse(Scanner, MLT_OPERATOR)) {
			mlc_const_call_expr_t *CallExpr = new(mlc_const_call_expr_t);
			CallExpr->compile = ml_const_call_expr_compile;
			CallExpr->Source = Scanner->Source;
			CallExpr->Value = (ml_value_t *)ml_method(Scanner->Ident);
			CallExpr->Child = Expr;
			Expr->Next = ml_accept_factor(Scanner);
			Expr = (mlc_expr_t *)CallExpr;
		} else if (ml_parse(Scanner, MLT_ASSIGN)) {
			mlc_parent_expr_t *AssignExpr = new(mlc_parent_expr_t);
			AssignExpr->compile = ml_assign_expr_compile;
			AssignExpr->Source = Scanner->Source;
			AssignExpr->Child = Expr;
			Expr->Next = ml_accept_expression(Scanner);
			Expr = (mlc_expr_t *)AssignExpr;
		} else {
			return Expr;
		}
	}
	return 0; // Unreachable
}

static mlc_expr_t *ml_accept_expression(mlc_scanner_t *Scanner) {
	while (ml_parse(Scanner, MLT_EOL));
	mlc_expr_t *Expr = ml_parse_expression(Scanner);
	if (Expr) return Expr;
	Scanner->ML->Error = ml_error("ParseError", "expected <expression> not %s", MLTokens[Scanner->Token]);
	ml_error_trace_add(Scanner->ML->Error, Scanner->Source);
	longjmp(Scanner->ML->OnError, 1);
}

static mlc_expr_t *ml_accept_block(mlc_scanner_t *Scanner) {
	mlc_decl_expr_t *BlockExpr = new(mlc_decl_expr_t);
	BlockExpr->compile = ml_block_expr_compile;
	BlockExpr->Source = Scanner->Source;
	mlc_expr_t **ExprSlot = &BlockExpr->Child;
	mlc_decl_t **DeclSlot = &BlockExpr->Decl;
	for (;;) {
		while (ml_parse(Scanner, MLT_EOL));
		if (ml_parse(Scanner, MLT_VAR)) {
			do {
				ml_accept(Scanner, MLT_IDENT);
				mlc_decl_t *Decl = DeclSlot[0] = new(mlc_decl_t);
				Decl->Ident = Scanner->Ident;
				DeclSlot = &Decl->Next;
				if (ml_parse(Scanner, MLT_ASSIGN)) {
					mlc_decl_expr_t *DeclExpr = new(mlc_decl_expr_t);
					DeclExpr->compile = ml_var_expr_compile;
					DeclExpr->Source = Scanner->Source;
					DeclExpr->Decl = Decl;
					DeclExpr->Child = ml_accept_expression(Scanner);
					ExprSlot[0] = (mlc_expr_t *)DeclExpr;
					ExprSlot = &DeclExpr->Next;
				}
			} while (ml_parse(Scanner, MLT_COMMA));
		} else {
			mlc_expr_t *Expr = ml_parse_expression(Scanner);
			if (!Expr) return (mlc_expr_t *)BlockExpr;
			ExprSlot[0] = Expr;
			ExprSlot = &Expr->Next;
		}
		ml_parse(Scanner, MLT_SEMICOLON);
	}
	return 0; // Unreachable
}

ml_type_t *ml_class(ml_type_t *Parent) {
	ml_type_t *Type = new(ml_type_t);
	Type->Parent = Parent;
	Type->hash = Parent->hash;
	Type->call = Parent->call;
	Type->index = Parent->index;
	Type->deref = Parent->deref;
	Type->assign = Parent->assign;
	Type->next = Parent->next;
	Type->key = Parent->key;
	return Type;
}

ml_value_t *ml_load(ml_t *ML, const char *FileName) {
	FILE *File = fopen(FileName, "r");
	mlc_scanner_t *Scanner = ml_scanner(ML, FileName, File, ml_file_read);
	if (setjmp(ML->OnError)) return ML->Error;
	mlc_expr_t *Expr = ml_accept_block(Scanner);
	ml_accept(Scanner, MLT_EOI);
	mlc_function_t Function[1] = {0,};
	Function->ML = ML;
	SHA256_CTX HashContext[1];
	sha256_init(HashContext);
	mlc_compiled_t Compiled = ml_compile(Function, Expr, HashContext);
	if (Compiled.Exits) ml_connect(Compiled.Exits, 0);
	ml_closure_t *Closure = new(ml_closure_t);
	ml_closure_info_t *Info = Closure->Info = new(ml_closure_info_t);
	Closure->Type = ClosureT;
	Info->Entry = Compiled.Start;
	Info->FrameSize = Function->Size;
	sha256_final(HashContext, Info->Hash);
	return (ml_value_t *)Closure;
}
