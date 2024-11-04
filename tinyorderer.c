#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define assert(x) \
  while (!(x)) __builtin_trap()

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;
typedef ptrdiff_t size;
typedef uintptr_t uptr;
typedef size_t usize;

#define new(a, t, n) (t *)pool_alloc(a, sizeof(t), _Alignof(t), n)

typedef struct {
  char *beg;
  char *end;
} arena;

void *alloc(arena *a, size objsize, size align, size count) {
  size padding = -(uptr)a->beg & (align - 1);
  size available = a->end - a->beg - padding;
  if (available < 0 || count > available / objsize) {
    assert(0 && "arena out of memory");
  }
  void *p = a->beg + padding;
  a->beg += padding + count * objsize;
  return memset(p, 0, count * objsize);
}

arena arena_new(size cap) {
  arena a = {0};
  a.beg = malloc(cap);
  a.end = a.beg ? a.beg + cap : 0;
  return a;
}

arena arena_new_mmap(size cap) {
  arena a = {0};
  a.beg =
      mmap(0, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  a.end = a.beg ? a.beg + cap : 0;
  return a;
}

typedef struct {
  arena *a;
  size len;
  size cap;
} arena_pool;

static arena_pool *pool_new() {
  arena_pool *p = malloc(sizeof(arena_pool));

  if (!p) {
    return 0;
  }

  p->a = malloc(1 * sizeof(arena));

  if (!p->a) {
    free(p);
    return 0;
  }

  p->a[0] = arena_new_mmap(4096);
  p->len = 1;
  p->cap = 1;

  return p;
}

void *pool_alloc(arena_pool *p, size objsize, size align, size count) {
  if (p->a[p->len - 1].beg + count * objsize > p->a[p->len - 1].end) {
    if (p->len == p->cap) {
      p->cap *= 2;
      p->a = realloc(p->a, p->cap * sizeof(arena));
    }

    p->a[p->len] = arena_new_mmap(4096);
    p->len++;
  }

  return alloc(&p->a[p->len - 1], objsize, align, count);
}

typedef struct {
  u8 index : 6;
  u8 spin : 1;
  u8 type : 1;
} operator;

static u8 op_data(operator a) { return *(u8 *)&a; }

static i32 op_less(operator a, operator b) { return op_data(a) < op_data(b); }

static i32 op_commutes(operator a, operator b) {
  return (a.type == b.type) || ((a.spin != b.spin) || (a.index != b.index));
}

static void op_print(operator a) {
  if (a.type == 0) {
    printf("c+");
  } else {
    printf("c-");
  }
  if (a.spin == 0) {
    printf("(up, %d)", a.index);
  } else {
    printf("(down, %d)", a.index);
  }
}

typedef struct {
  operator* data;
  size len;
} op_list;

static op_list op_list_new(arena_pool *p, size len) {
  op_list t = {0};
  t.data = new (p, operator, len);
  t.len = len;
  return t;
}

static op_list op_list_copy(arena_pool *p, op_list t) {
  op_list c = op_list_new(p, t.len);
  memcpy(c.data, t.data, t.len * sizeof(operator));
  return c;
}

static op_list op_list_erase(arena_pool *p, op_list t, size i, size j) {
  op_list c = op_list_new(p, t.len - j + i);
  memcpy(c.data, t.data, i * sizeof(operator));
  memcpy(c.data + i, t.data + j, (t.len - j) * sizeof(operator));
  return c;
}

static op_list op_list_swap(arena_pool *p, op_list t, size i, size j) {
  op_list c = op_list_copy(p, t);
  operator tmp = c.data[i];
  c.data[i] = c.data[j];
  c.data[j] = tmp;
  return c;
}

static i32 op_list_compare(op_list a, op_list b) {
  if (a.len != b.len) {
    return a.len - b.len;
  }
  return memcmp(a.data, b.data, a.len * sizeof(operator));
}

typedef struct {
  f64 coeff;
  op_list lst;
} term;

static term term_copy(arena_pool *p, term t) {
  term c = {0};
  c.coeff = t.coeff;
  c.lst = op_list_copy(p, t.lst);
  return c;
}

static void term_print(term t) {
  printf("%f ", t.coeff);
  for (size i = 0; i < t.lst.len; i++) {
    op_print(t.lst.data[i]);
    printf(" ");
  }
  printf("\n");
}

typedef struct {
  term *data;
  size len;
} term_list;

static term_list term_list_new(arena_pool *p, size len) {
  term_list t = {0};
  t.data = new (p, term, len);
  t.len = len;
  return t;
}

static term_list term_list_merge(arena_pool *p, term_list t, term_list u) {
  term_list c = term_list_new(p, t.len + u.len);
  i32 i = 0, j = 0, k = 0;

  while (i < t.len && j < u.len) {
    int cmp = op_list_compare(t.data[i].lst, u.data[j].lst);
    if (cmp == 0) {
      f64 coeff = t.data[i].coeff + u.data[j].coeff;
      if (coeff != 0) {
        c.data[k] = term_copy(p, t.data[i]);
        c.data[k].coeff = coeff;
        k++;
      }
      i++;
      j++;
    } else if (cmp < 0) {
      c.data[k] = term_copy(p, t.data[i]);
      i++;
      k++;
    } else {
      c.data[k] = term_copy(p, u.data[j]);
      j++;
      k++;
    }
  }

  while (i < t.len) {
    c.data[k] = term_copy(p, t.data[i]);
    i++;
    k++;
  }

  while (j < u.len) {
    c.data[k] = term_copy(p, u.data[j]);
    j++;
    k++;
  }

  c.len = k;
  return c;
}

static i32 term_list_equal(term_list a, term_list b) {
  if (a.len != b.len) {
    return 0;
  }
  for (size i = 0; i < a.len; i++) {
    if (a.data[i].coeff != b.data[i].coeff) {
      return 0;
    }
    if (op_list_compare(a.data[i].lst, b.data[i].lst) != 0) {
      return 0;
    }
  }
  return 1;
}

term_list normal_orderer(arena_pool *p, term t) {
  if (t.lst.len < 2) {
    term_list res = term_list_new(p, 1);
    res.data[0] = term_copy(p, t);
    return res;
  }

  for (size i = 1; i < t.lst.len; i++) {
    size j = i;
    while (j > 0 && op_less(t.lst.data[j], t.lst.data[j - 1])) {
      if (op_commutes(t.lst.data[j], t.lst.data[j - 1])) {
        operator tmp = t.lst.data[j];
        t.lst.data[j] = t.lst.data[j - 1];
        t.lst.data[j - 1] = tmp;
        j--;
        t.coeff *= -1;
      } else {
        op_list contracted = op_list_erase(p, t.lst, j - 1, j + 1);
        term_list left = normal_orderer(p, (term){t.coeff, contracted});

        op_list swapped = op_list_swap(p, t.lst, j - 1, j);
        term_list right = normal_orderer(p, (term){-t.coeff, swapped});

        term_list merged = term_list_merge(p, left, right);
        return merged;
      }
    }
  }
  term_list res = term_list_new(p, 1);
  res.data[0] = term_copy(p, t);
  return res;
}

term_list merge_ordered_terms(arena_pool *p, term_list left, term_list right,
                              f64 coeff) {
  term_list result = term_list_new(p, left.len * right.len);
  size len = 0;

  for (size i = 0; i < left.len; i++) {
    for (size j = 0; j < right.len; j++) {
      op_list combined =
          op_list_new(p, left.data[i].lst.len + right.data[j].lst.len);
      memcpy(combined.data, left.data[i].lst.data,
             left.data[i].lst.len * sizeof(operator));
      memcpy(combined.data + left.data[i].lst.len, right.data[j].lst.data,
             right.data[j].lst.len * sizeof(operator));

      term combined_term = (term){coeff, combined};
      term_list ordered = normal_orderer(p, combined_term);

      memcpy(result.data + len, ordered.data, ordered.len * sizeof(term));
      len += ordered.len;
    }
  }

  result.len = len;
  return result;
}

term_list normal_orderer2(arena_pool *p, term t) {
  if (t.lst.len < 2) {
    term_list res = term_list_new(p, 1);
    res.data[0] = term_copy(p, t);
    return res;
  }

  size mid = t.lst.len / 2;

  op_list left = op_list_new(p, mid);
  memcpy(left.data, t.lst.data, mid * sizeof(operator));

  op_list right = op_list_new(p, t.lst.len - mid);
  memcpy(right.data, t.lst.data + mid, (t.lst.len - mid) * sizeof(operator));

  term_list left_terms = normal_orderer2(p, (term){1.0, left});
  term_list right_terms = normal_orderer2(p, (term){1.0, right});

  term_list merged = merge_ordered_terms(p, left_terms, right_terms, t.coeff);

  return merged;
}

int main(int argc, char **argv) {
  arena_pool *p = pool_new();

  const size N = 2;

  operator ops[2*N];

  for (size i = 0; i < N/2; i++) {
    ops[i] = (operator){.type=1, .spin=0, .index=0};
    ops[i+1] = (operator){.type=0, .spin=0, .index=0};
  }

  op_list lst = {ops, 2*N};
  term t = (term){1, lst};
  term_print(t);

  term_list res1 = normal_orderer(p, t);
  term_list res2 = normal_orderer2(p, t);
  
  for (size i = 0; i < res1.len; i++) {
    term_print(res1.data[i]);
  }

  printf("\n");
  for (size i = 0; i < res1.len; i++) {
    term_print(res1.data[i]);
  }
  
  return 0;
}