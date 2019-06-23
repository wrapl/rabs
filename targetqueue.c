#include "targetqueue.h"
#include "minilang/ml_macros.h"
#include <gc.h>
#include <string.h>

target_t **QueueHeap = 0;
target_t *PriorityAdjustQueue = 0;
int QueueSize = 0;
int QueueTop = 0;

void targetqueue_init() {
	QueueSize = 32;
	QueueHeap = anew(target_t *, QueueSize);
}

static void target_priority_compute(target_t *Target);

static int targetqueue_priority_fn(target_t *Target, int *Total) {
	if (Target->QueuePriority == PRIORITY_INVALID) target_priority_compute(Target);
	*Total += Target->QueuePriority;
	return 0;
}

static void target_priority_compute(target_t *Target) {
	Target->QueuePriority = 1;
	targetset_foreach(Target->Affects, &Target->QueuePriority, (void *)targetqueue_priority_fn);
}

void targetqueue_insert(target_t *Target) {
	if (Target->QueuePriority == PRIORITY_INVALID) target_priority_compute(Target);
	if (QueueTop == QueueSize) {
		int NewHeapSize = QueueSize * 2;
		target_t **NewHeap = anew(target_t *, NewHeapSize);
		memcpy(NewHeap, QueueHeap, QueueSize * sizeof(target_t *));
		QueueHeap = NewHeap;
		QueueSize = NewHeapSize;
	}
	int Index = QueueTop++;
	QueueHeap[Index] = Target;
	while (Index > 0) {
		int ParentIndex = (Index - 1) / 2;
		target_t *Parent = QueueHeap[ParentIndex];
		if (Parent->QueuePriority >= Target->QueuePriority) {
			Target->QueueIndex = Index;
			return;
		}
		Parent->QueueIndex = Index;
		QueueHeap[Index] = Parent;
		QueueHeap[ParentIndex] = Target;
		Index = ParentIndex;
	}
}

void target_priority_invalidate(target_t *Target) {
	if ((Target->QueueIndex >= 0) && (Target->QueuePriority > PRIORITY_INVALID)) {
		Target->QueuePriority = PRIORITY_INVALID;
		Target->PriorityAdjustNext = PriorityAdjustQueue;
		PriorityAdjustQueue = Target;
	}
}

static inline void targetqueue_adjust_queue() {
	target_t *Target = PriorityAdjustQueue;
	PriorityAdjustQueue = 0;
	while (Target) {
		if (Target->QueuePriority == PRIORITY_INVALID) {
			target_priority_compute(Target);
			int Index = Target->QueueIndex;
			while (Index > 0) {
				int ParentIndex = (Index - 1) / 2;
				target_t *Parent = QueueHeap[ParentIndex];
				if (Parent->QueuePriority >= Target->QueuePriority) {
					Target->QueueIndex = Index;
					return;
				}
				Parent->QueueIndex = Index;
				QueueHeap[Index] = Parent;
				QueueHeap[ParentIndex] = Target;
				Index = ParentIndex;
			}
		}
		Target = Target->PriorityAdjustNext;
	}
}

target_t *targetqueue_next() {
	targetqueue_adjust_queue();
	//printf("Queue state:\n");
	//for (int Index = 0; Index < QueueTop; ++Index) printf("\t%d -> %d -> %s\n", Index, QueueHeap[Index]->QueuePriority, QueueHeap[Index]->Id);
	target_t *Next = QueueHeap[0];
	if (!Next) return 0;
	Next->QueueIndex = -2;
	int Index = 0;
	target_t *Target = QueueHeap[--QueueTop];
	QueueHeap[QueueTop] = 0;
	if (!QueueTop) return Next;
	for (;;) {
		if (Index >= QueueTop) asm("int3");
		int Left = 2 * Index + 1;
		int Right = 2 * Index + 2;
		int Largest = Index;
		QueueHeap[Index] = Target;
		if (Left < QueueTop && QueueHeap[Left] && QueueHeap[Left]->QueuePriority > QueueHeap[Largest]->QueuePriority) {
			Largest = Left;
		}
		if (Right < QueueTop && QueueHeap[Right] && QueueHeap[Right]->QueuePriority > QueueHeap[Largest]->QueuePriority) {
			Largest = Right;
		}
		if (Largest != Index) {
			target_t *Parent = QueueHeap[Largest];
			QueueHeap[Index] = Parent;
			Parent->QueueIndex = Index;
			Index = Largest;
		} else {
			Target->QueueIndex = Index;
			return Next;
		}
	}
}
