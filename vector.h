#ifndef VECTOR_H
#define VECTOR_H
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct vector_t {
    size_t size;
    size_t capacity;
    char** arr;
} Vector;


Vector vectorInit(size_t capacity);
bool vectorInsert(Vector* pVector, char* string, size_t size);
bool vectorRemove(Vector* pVector, char* string, size_t size);
void vectorDestroy(Vector* pVector);

#endif
