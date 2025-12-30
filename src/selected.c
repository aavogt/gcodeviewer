#include "selected.h"
#include <assert.h>
#include <stdio.h>

size_t selected[MAXSEL], *send, *s;

void selected_init() {
  for (int i = 0; i < MAXSEL; i++)
    selected[i] = SELECTED_EMPTY;
  send = selected + (MAXSEL - 1);
  s = selected;
}

int selected_count() {
  int count = 0;
  for (int i = 0; i < MAXSEL; i++) {
    if (selected[i] != SELECTED_EMPTY)
      count++;
  }
  return count;
}

void selected_add(size_t x) {
  *s++ = x;
  if (s > send)
    s = selected;
}

void selected_remove(size_t x) {
  int pos = -1;
  for (int i = 0; i < MAXSEL; i++) {
    if (selected[i] == x) {
      pos = i;
      break;
    }
  }
  if (pos < 0)
    return;

  int count = selected_count();
  int j = pos;

  if (count == MAXSEL) {
    for (int k = 0; k < MAXSEL - 1; k++) {
      int from = (j + 1) % MAXSEL;
      selected[j] = selected[from];
      j = from;
    }
    selected[j] = SELECTED_EMPTY;
    s = selected + j;
  } else {
    int from = (j + 1) % MAXSEL;
    while (selected[from] != SELECTED_EMPTY) {
      selected[j] = selected[from];
      j = from;
      from = (from + 1) % MAXSEL;
    }
    selected[j] = SELECTED_EMPTY;
    s = selected + j;
  }
}

bool selected_find(size_t x) {
  for (int i = 0; i < MAXSEL; i++) {
    if (selected[i] == x)
      return true;
  }
  return false;
}

#ifdef TESTING
int main() {
  selected_init();

  // Fill to capacity with unique values
  for (int i = 0; i < MAXSEL; i++) {
    selected_add((size_t)i);
    assert(selected_find((size_t)i));
  }

  // Insert up to 3*MAXSEL total values; only the last MAXSEL remain
  for (int i = MAXSEL; i < 3 * MAXSEL; i++) {
    selected_add((size_t)i);
  }

  // The first 2*MAXSEL values should be missing; the last MAXSEL present
  for (int i = 0; i < 2 * MAXSEL; i++) {
    assert(!selected_find((size_t)i));
  }
  for (int i = 2 * MAXSEL; i < 3 * MAXSEL; i++) {
    assert(selected_find((size_t)i));
  }
  assert(selected_count() == MAXSEL);

  // Removing an existing value reduces count and makes it missing
  size_t x = (size_t)(2 * MAXSEL);
  selected_remove(x);
  assert(selected_count() == MAXSEL - 1);
  assert(!selected_find(x));

  // Removing a non-existing value does nothing
  int prev = selected_count();
  selected_remove((size_t)0);
  assert(selected_count() == prev);
  assert(!selected_find((size_t)0));

  // Duplicates behave like a multiset: removing one occurrence keeps others
  selected_init();
  size_t v = (size_t)123456;
  selected_add(v);
  selected_add(v);
  assert(selected_count() == 2);
  assert(selected_find(v));
  selected_remove(v);
  assert(selected_count() == 1);
  assert(selected_find(v));
  selected_remove(v);
  assert(selected_count() == 0);
  assert(!selected_find(v));

  printf("ok\n");
}
#endif
