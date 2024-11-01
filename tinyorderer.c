#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define assert(x) \
  while (!(x)) __builtin_trap()

typedef uint8_t u8;
typedef ptrdiff_t size;
typedef uintptr_t uptr;

#define new(a, t, n) (t *)alloc(a, sizeof(t), _Alignof(t), n)

typedef struct {
  u8 *beg;
  u8 *end;
} arena;

static u8 *alloc(arena *a, size objsize, size align, size count) {
  assert(count >= 0);
  size pad = -(uptr)a->beg & (align - 1);
  assert(count < (a->end - a->beg - pad) / objsize);
  void *p = a->beg + pad;
  a->end += count * objsize + pad;
  return memset(p, 0, count * objsize);
}

static arena new_arena(size size) {
  arena a = {0};
  a.beg = malloc(size);
  a.end = a.beg ? a.beg + size : 0;
  return a;
}

typedef struct {
  u8 index : 6;
  u8 spin : 1;
  u8 type : 1;
} operator;

int main(int argc, char **argv) {}