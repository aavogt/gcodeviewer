#pragma once
#include <stdbool.h>
#include <stddef.h>

#define MAXSEL 100
#define SELECTED_EMPTY ((size_t)-1)

/// circular buffer for the line segment selection
extern size_t selected[MAXSEL], sselected[MAXSEL];

void selected_init();

int selected_count();

void selected_add(size_t x);

void selected_remove(size_t x);

bool selected_find(size_t x);
