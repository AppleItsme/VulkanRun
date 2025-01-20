#include <utils.h>
#include <stdlib.h>
#include <string.h>

void EngineCreateQueue(EngineQueue *queue) {
    queue->arr = malloc(queue->length * queue->byteSize);
    queue->count = 0;
}

void EngineDestroyQueue(EngineQueue *queue) {
    free(queue->arr);
}
void EngineQueueAdd(EngineQueue *queue, void *in) {
    if(queue->length == queue->count) {
        queue->length *= 2;
        queue->arr = realloc(queue->arr, queue->length * queue->byteSize);
    }
    memcpy((char*)queue->arr + queue->count * queue->byteSize, in, queue->byteSize);
    queue->count++;
}
void EngineQueueRetreive(EngineQueue *queue, void *out) {
    memcpy(out, queue->arr, queue->byteSize);
    for(size_t i = 1; i < queue->count; i++) {
        memcpy((char*)queue->arr + (i-1)*queue->byteSize, (char*)queue->arr + i*queue->byteSize, queue->byteSize); 
    }
    queue->count--;
}
void EngineQueuePeek(EngineQueue *queue, void *out) {
    memcpy(out, queue->arr, queue->byteSize);
}