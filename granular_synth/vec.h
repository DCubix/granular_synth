#pragma once

#include <stdio.h>
#include <assert.h>

#define VEC_INITIAL_CAPACITY 100

typedef struct _vec_memblock_t {
	void* data;
	size_t size;
} vec_memblock_t;

typedef struct _vec_t {
	vec_memblock_t* elements;
	size_t size, capacity;
} vec_t;

vec_t* vec_new();
void vec_push(vec_t* vec, void* element, size_t size);
void* vec_get(vec_t* vec, size_t index);

void vec_check_and_realloc(vec_t* vec);

#ifdef VEC_IMPL
vec_t* vec_new() {
	vec_t* vec = (vec_t*)malloc(sizeof(vec_t));
	vec->capacity = VEC_INITIAL_CAPACITY;
	vec->size = 0;
	vec->elements = (vec_memblock_t*)calloc(vec->capacity, sizeof(vec_memblock_t));
	return vec;
}

void vec_push(vec_t* vec, void* element, size_t size) {
	vec_memblock_t* block = &vec->elements[vec->size++];
	vec_check_and_realloc(vec);

	block->size = size;
	block->data = element;
}

#define vec_push_value(vec, type, value) { \
	void* data = malloc(sizeof(type)); \
	type _tmp = value; \
	memcpy(data, &_tmp, sizeof(type)); \
	vec_push(vec, data, sizeof(type)); \
}

void* vec_get(vec_t* vec, size_t index) {
	assert(index < vec->size);
	return vec->elements[index].data;
}

#define vec_get_value(vec, type, index) (*((type*)vec_get(vec, index)))

void vec_check_and_realloc(vec_t* vec) {
	if (vec->size < vec->capacity) {
		return;
	}
	vec->capacity *= 2;
	vec->elements = (vec_memblock_t*)realloc(vec->elements, sizeof(vec_memblock_t) * vec->capacity);
}

#endif