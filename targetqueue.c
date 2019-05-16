#include "targetqueue.h"
#include "minilang/ml_macros.h"
#include <gc.h>
#include <string.h>

target_t **QueueHeap = 0;
int QueueSize = 0;
int QueueTop = 0;

void targetqueue_init() {
	QueueSize = 32;
	QueueHeap = anew(target_t *, QueueSize);
}

void targetqueue_insert(target_t *Target) {
	int Index = Target->QueueIndex;
	if (Index < 0) {
		if (QueueTop == QueueSize) {
			int NewHeapSize = QueueSize * 2;
			target_t **NewHeap = anew(target_t *, NewHeapSize);
			memcpy(NewHeap, QueueHeap, QueueSize * sizeof(target_t *));
			QueueHeap = NewHeap;
			QueueSize = NewHeapSize;
		}
		Index = QueueTop++;
		QueueHeap[Index] = Target;
	}
	while (Index > 0) {
		int ParentIndex = (Index - 1) / 2;
		target_t *Parent = QueueHeap[ParentIndex];
		if (Parent->QueuePriority <= Target->QueuePriority) {
			Target->QueueIndex = Index;
			return;
		}
		Parent->QueueIndex = Index;
		QueueHeap[Index] = Parent;
		QueueHeap[ParentIndex] = Target;
		Index = ParentIndex;
	}
}

target_t *targetqueue_next() {
	target_t *Next = QueueHeap[0];
	if (!Next) return 0;
	Next->QueueIndex = -1;
	int Index = 0;
	target_t *Target = QueueHeap[--QueueTop];
	QueueHeap[QueueTop] = 0;
	if (!QueueTop) return Next;
	for (;;) {
		int Left = 2 * Index + 1;
		int Right = 2 * Index + 2;
		if (QueueHeap[Left] && QueueHeap[Left]->QueuePriority > Target->QueuePriority) {
			if (QueueHeap[Right] && QueueHeap[Right]->QueuePriority > QueueHeap[Left]->QueuePriority) {
				target_t *Parent = QueueHeap[Index] = QueueHeap[Right];
				Parent->QueueIndex = Index;
				Index = Right;
			} else {
				target_t *Parent = QueueHeap[Index] = QueueHeap[Left];
				Parent->QueueIndex = Index;
				Index = Left;
			}
		} else if (QueueHeap[Right] && QueueHeap[Right]->QueuePriority > Target->QueuePriority) {
			target_t *Parent = QueueHeap[Index] = QueueHeap[Right];
			Parent->QueueIndex = Index;
			Index = Right;
		} else {
			QueueHeap[Index] = Target;
			Target->QueueIndex = Index;
			return Next;
		}
	}
}
