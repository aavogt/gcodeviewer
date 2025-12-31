#pragma once
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAXSEL 100
#define SELECTED_EMPTY SIZE_MAX

/// circular buffer for the line segment selection
extern size_t selected[MAXSEL], sselected[MAXSEL];

void selected_init();

int selected_count();

void selected_add(size_t x);

void selected_remove(size_t x);

bool selected_find(size_t x);

size_t selected_index(size_t x);

bool selected_pop(size_t *x);
