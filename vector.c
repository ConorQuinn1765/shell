#include <string.h>
#include "vector.h"

bool resizeArray(Vector* pVector);

Vector vectorInit(size_t capacity)
{
    // If no capacity is provided, use default value
    if(capacity == 0)
        capacity = 8;

    Vector vect = {0, capacity, NULL};
    vect.arr = calloc(vect.capacity, sizeof(char*));
    if(!vect.arr) {
        vect.capacity = 0;
        return vect;
    }

    return vect;
}

bool vectorInsert(Vector* pVector, char* string, size_t size)
{
    if(!pVector || !string)
        return false;

    if(pVector->size >= pVector->capacity)
        if(!resizeArray(pVector))
            return false;

    pVector->arr[pVector->size] = calloc(size + 1, sizeof(char));
    if(!pVector->arr[pVector->size])
        return false;

    strncpy(pVector->arr[pVector->size], string, size);
    pVector->size++;

    return true;
}

bool vectorRemove(Vector* pVector, char* string, size_t size)
{
    if(!pVector || !string)
        return false;

    if(pVector->size <= 0)
        return true;

    for(size_t i = 0; i < pVector->size; i++) {
        if(strncmp(pVector->arr[i], string, size) == 0) {
            free(pVector->arr[i]);

            for(size_t j = i; j < pVector->size - 1; j++)
                pVector->arr[j] = pVector->arr[j+1];

            pVector->arr[pVector->size] = NULL;
            break;
        }
    }

    pVector->size--;
    return true;
}

void vectorDestroy(Vector* pVector)
{
    if(pVector) {
        for(size_t i = 0; i < pVector->size; i++)
            free(pVector->arr[i]);
        free(pVector->arr);
    }
}

bool resizeArray(Vector* pVector)
{
    if(!pVector)
        return false;

    char** temp = calloc(pVector->capacity * 2, sizeof(char*));
    if(!temp)
        return false;

    for(size_t i = 0; i < pVector->size; i++)
        temp[i] = pVector->arr[i];

    free(pVector->arr);
    pVector->arr = temp;
    pVector->capacity *= 2;
    return true;
}
