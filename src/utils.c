#include <utils.h>
#include <stdlib.h>
#include <string.h>

void EngineCreateHeapArray(EngineHeapArray *heapArr) {
    heapArr->arr = malloc(heapArr->length * heapArr->byteSize);
    heapArr->count = 0;
}

void EngineDestroyHeapArray(EngineHeapArray *heapArr) {
    free(heapArr->arr);
}
void EngineHeapArrayEnqueue(EngineHeapArray *heapArr, void *in) {
    if(heapArr->length == heapArr->count) {
        heapArr->length *= 2;
        heapArr->arr = realloc(heapArr->arr, heapArr->length * heapArr->byteSize);
    }
    memcpy((char*)heapArr->arr + heapArr->count * heapArr->byteSize, in, heapArr->byteSize);
    heapArr->count++;
}
void EngineHeapArrayDequeue(EngineHeapArray *heapArr, void *out) {
    memcpy(out, heapArr->arr, heapArr->byteSize);
    for(size_t i = 1; i < heapArr->count; i++) {
        memcpy((char*)heapArr->arr + (i-1)*heapArr->byteSize, (char*)heapArr->arr + i*heapArr->byteSize, heapArr->byteSize); 
    }
    heapArr->count--;
}

void EngineHeapArrayPop(EngineHeapArray *heapArr, void *out) {
    heapArr->count--;
    memcpy(out,(char*)heapArr->arr + heapArr->count*heapArr->byteSize, sizeof(heapArr->byteSize)); 
}