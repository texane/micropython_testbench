#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>


/* py exported API */
/* micropython based implementation */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include "py/mpstate.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/mphal.h"
#include "py/objint.h"
#include "genhdr/mpversion.h"


typedef struct
{
  mp_obj_base_t base;
  mp_float_t value;
} mp_obj_float_t;


static void mp_obj_set_float(mp_obj_t o, mp_float_t x)
{
  ((mp_obj_float_t*)MP_OBJ_TO_PTR(o))->value = x;
}


#define PY_FLAG_ARRAY (1 << 0)
#define PY_FLAG_IN (1 << 1)
#define PY_FLAG_OUT (1 << 2)
#define PY_FLAG_INOUT (PY_FLAG_IN | PY_FLAG_OUT)
#define PY_FLAG_INT (1 << 3)
#define PY_FLAG_FLOAT (1 << 4)


typedef struct py_var
{
  qstr name;

  uint32_t flags;
  size_t dim;
  
  mp_obj_t list;
  mp_obj_t* items;
  mp_obj_t index;

  union
  {
    mp_float_t* f;
    mp_int_t* i;
  } in;

  union
  {
    mp_float_t* f;
    mp_int_t* i;
  } out;

} py_var_t;


typedef struct py_handle
{
  /* micropython */
  mp_lexer_t* lex;
  mp_parse_tree_t parse_tree;
  mp_obj_t module_fun;

  /* inout variables */
  size_t nvar;
  py_var_t* vars[32];

} py_handle_t;


static int py_init(void)
{
  static mp_uint_t path_num = 1;
  mp_obj_t *path_items;

  mp_stack_set_limit(40000 * (BYTES_PER_WORD / 4));

#if MICROPY_ENABLE_GC
  static long heap_size = 1024 * 1024 * (sizeof(mp_uint_t) / 4);
  char *heap = malloc(heap_size);
  gc_init(heap, heap + heap_size);
#endif

  mp_init();

  mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_path), path_num);
  mp_obj_list_get(mp_sys_path, &path_num, &path_items);
  path_items[0] = MP_OBJ_NEW_QSTR(MP_QSTR_);
  mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);

  return 0;
}


static void py_fini(void)
{
  /* TODO: free heap */
  mp_deinit();
}


static py_var_t* py_create_var
(py_handle_t* py, const char* name, size_t dim, uint32_t flags)
{
  py_var_t* v;
  mp_obj_t obj;
  size_t i;
  size_t size;
  void** inp;
  void** outp;

  v = malloc(sizeof(py_var_t));
  if (v == NULL) goto on_error_0;

  py->vars[py->nvar] = v;
  ++py->nvar;

  v->name = qstr_from_str(name);
  v->flags = flags;
  v->dim = dim;

  v->items = malloc(dim * sizeof(mp_obj_t));
  if (v->items == NULL) goto on_error_1;

  if (v->flags & PY_FLAG_INT)
  {
    for (i = 0; i != dim; ++i) v->items[i] = MP_OBJ_NEW_SMALL_INT(0);
  }
  else
  {
    for (i = 0; i != dim; ++i) v->items[i] = mp_obj_new_float(0.0);
  }

  v->index = MP_OBJ_NEW_SMALL_INT(0);

  /* store default values so that no crash due to undefined variable */

  if (v->flags & PY_FLAG_ARRAY)
  {
    v->list = mp_obj_new_list((mp_uint_t)v->dim, v->items);
    obj = v->list;
  }
  else
  {
    obj = v->items[0];
  }

  mp_store_name(v->name, obj);

  if (v->flags & PY_FLAG_FLOAT)
  {
    size = dim * sizeof(mp_float_t);
    inp = (void**)&v->in.f;
    outp = (void**)&v->out.f;
  }
  else
  {
    size = dim * sizeof(mp_int_t);
    inp = (void**)&v->in.i;
    outp = (void**)&v->out.i;
  }

  if (v->flags & PY_FLAG_IN)
  {
    *inp = malloc(size);
    if (*inp == NULL) goto on_error_2;
  }

  if (v->flags & PY_FLAG_OUT)
  {
    *outp = malloc(size);
    if (*outp == NULL) goto on_error_3;
  }
  
  return v;

 on_error_3:
  if (v->flags & PY_FLAG_IN) free(*inp);
 on_error_2:
  free(v->items);
 on_error_1:
  free(v);
 on_error_0:
  return NULL;
}

static py_var_t* py_create_scalar
(py_handle_t* py, const char* name, uint32_t flags)
{
  return py_create_var(py, name, 1, flags);
}

static py_var_t* py_create_array
(py_handle_t* py, const char* name, size_t dim, uint32_t flags)
{
  return py_create_var(py, name, dim, flags | PY_FLAG_ARRAY);
}

static void py_destroy_var(py_var_t* v)
{
  void** inp;
  void** outp;

  if (v->flags & PY_FLAG_FLOAT)
  {
    inp = (void**)&v->in.f;
    outp = (void**)&v->out.f;
  }
  else
  {
    inp = (void**)&v->in.i;
    outp = (void**)&v->out.i;
  }

  if (v->flags & PY_FLAG_IN) free(*inp);
  if (v->flags & PY_FLAG_OUT) free(*outp);

  free(v->items);
  free(v);
}

static int py_compile(py_handle_t* py, const char* s)
{
  static const mp_parse_input_kind_t input_kind = MP_PARSE_FILE_INPUT;
  static const uint emit_opt = MP_EMIT_OPT_NATIVE_PYTHON;
  nlr_buf_t nlr;
  int err = -1;

  if (nlr_push(&nlr)) return -1;

  py->lex = mp_lexer_new_from_str_len
    (MP_QSTR__lt_stdin_gt_, s, strlen(s), false);
  if (py->lex == NULL) goto on_error_0;

  py->parse_tree = mp_parse(py->lex, input_kind);

  py->module_fun = mp_compile
    (&py->parse_tree, py->lex->source_name, emit_opt, false);

  err = 0;

 on_error_0:
  nlr_pop();
  return err;
}

static int py_open(py_handle_t* py)
{
  py->lex = NULL;
  py->nvar = 0;
  return 0;
}

static int py_close(py_handle_t* py)
{
  size_t i;

  for (i = 0; i != py->nvar; ++i) py_destroy_var(py->vars[i]);

  return 0;
}

static int py_store_vars(py_handle_t* py)
{
  mp_obj_t obj;
  size_t i;
  size_t j;

  for (i = 0; i != py->nvar; ++i)
  {
    py_var_t* const v = py->vars[i];

    if ((v->flags & PY_FLAG_IN) == 0) continue ;

    if (v->flags & PY_FLAG_ARRAY)
    {
      if (v->flags & PY_FLAG_INT)
      {
	for (j = 0; j != v->dim; ++j)
	{
	  v->items[j] = MP_OBJ_NEW_SMALL_INT(v->in.i[j]);
	  v->index = MP_OBJ_NEW_SMALL_INT(j);
	  mp_obj_list_store(v->list, v->index, v->items[j]);
	}
      }
      else
      {
	for (j = 0; j != v->dim; ++j)
	{
	  mp_obj_set_float(v->items[j], v->in.f[j]);
	  v->index = MP_OBJ_NEW_SMALL_INT(j);
	  mp_obj_list_store(v->list, v->index, v->items[j]);
	}
      }

      obj = v->list;
    }
    else /* scalar */
    {
      if (v->flags & PY_FLAG_INT)
	v->items[0] = MP_OBJ_NEW_SMALL_INT(v->in.i[0]);
      else
	mp_obj_set_float(v->items[0], v->in.f[0]);
      obj = v->items[0];
    }

    mp_store_name(v->name, obj);
  }

  return 0;
}

static int py_load_vars(py_handle_t* py)
{
  mp_obj_t obj;
  mp_obj_t* items;
  mp_uint_t n;
  size_t i;
  size_t j;

  for (i = 0; i != py->nvar; ++i)
  {
    py_var_t* const v = py->vars[i];

    if ((v->flags & PY_FLAG_OUT) == 0) continue ;

    obj = mp_load_name(v->name);

    if (v->flags & PY_FLAG_ARRAY)
    {
      mp_obj_list_get(obj, &n, &items);
      if (n > v->dim) n = v->dim;
    }
    else
    {
      items = &obj;
      n = 1;
    }

    if (v->flags & PY_FLAG_INT)
    {
      for (j = 0; j != (size_t)n; ++j)
	v->out.i[j] = mp_obj_get_int(items[j]);
    }
    else
    {
      for (j = 0; j != (size_t)n; ++j)
	v->out.f[j] = mp_obj_float_get(items[j]);
    }
  }

  return 0;
}

static int py_execute(py_handle_t* py)
{
  nlr_buf_t nlr;
  int err = -1;

  if (nlr_push(&nlr)) return -1;

  if (py_store_vars(py)) goto on_error;
  mp_call_function_0(py->module_fun);
  if (py_load_vars(py)) goto on_error;

  err = 0;
 on_error:
  nlr_pop();
  return err;
}


/* tests */

struct test_desc
{
  const char* name;
  const char* py_str;
  int (*pre_compile)(py_handle_t*);
  int (*pre_exec)(py_handle_t*);
  void (*c)(py_handle_t*);
  void (*print)(py_handle_t*);
};

#define TEST_DESC(__name)			\
{						\
 .name = #__name,				\
 .py_str = __name ## _py,			\
 .pre_compile = __name ## _pre_compile,		\
 .pre_exec = __name ## _pre_exec,		\
 .c = __name ## _c,				\
 .print = __name ## _print			\
}

#define TEST_DESC_INVALID			\
{						\
 .name = NULL					\
}

#define TEST_LINE(__s) __s "\n"


#if 1 /* matmul */

static const char* matmul_py =						\
TEST_LINE("@micropython.viper")						\
TEST_LINE("def matmul(y, a, x, b):")					\
TEST_LINE("    n = int(len(y))")					\
TEST_LINE("    for i in range(0, n):")					\
TEST_LINE("        y[i] = 0.0")						\
TEST_LINE("        for j in range(0, n): y[i] += a[i * n + j] * x[j]")	\
TEST_LINE("        y[i] += b[i]")					\
TEST_LINE("matmul(_y, _a, _x, _b)");

static void matmul_c(py_handle_t* py)
{
  mp_float_t* const y = py->vars[0]->out.f;
  const mp_float_t* const a = py->vars[1]->in.f;
  const mp_float_t* const x = py->vars[2]->in.f;
  const mp_float_t* const b = py->vars[3]->in.f;
  size_t n = py->vars[0]->dim;

  size_t i;
  size_t j;

  for (i = 0; i != n; ++i)
  {
    y[i] = 0;
    for (j = 0; j != n; ++j) y[i] += a[i * n + j] * x[j];
    y[i] += b[i];
  }
}

static int matmul_pre_compile(py_handle_t* py)
{
  static const size_t n = 8;

  py_create_array(py, "_y", n, PY_FLAG_FLOAT | PY_FLAG_OUT);
  py_create_array(py, "_a", n * n, PY_FLAG_FLOAT | PY_FLAG_IN);
  py_create_array(py, "_x", n, PY_FLAG_FLOAT | PY_FLAG_IN);
  py_create_array(py, "_b", n, PY_FLAG_FLOAT | PY_FLAG_IN);

  return 0;
}

static int matmul_pre_exec(py_handle_t* py)
{
  const size_t n = py->vars[0]->dim;
  const size_t nn = n * n;

  mp_float_t* const y = py->vars[0]->out.f;
  mp_float_t* const a = py->vars[1]->in.f;
  mp_float_t* const x = py->vars[2]->in.f;
  mp_float_t* const b = py->vars[3]->in.f;

  size_t i;

  for (i = 0; i != n; ++i) y[i] = 0.0;

  for (i = 0; i != nn; ++i) a[i] = 0.5;
  for (i = 0; i != n; ++i) x[i] = (mp_float_t)i;
  for (i = 0; i != n; ++i) b[i] = (mp_float_t)i + 1000.42;

  return 0;
}

static void matmul_print(py_handle_t* py)
{
  const size_t n = py->vars[0]->dim;
  mp_float_t* const y = py->vars[0]->out.f;

  size_t i;

  for (i = 0; i != n; ++i) printf(" %lf", y[i]);
  printf("\n");
}

#endif /* matmul */


#if 1 /* hi */

static const char* hi_py = TEST_LINE("print('hi')");
static void hi_c(py_handle_t* py) {}
static int hi_pre_compile(py_handle_t* py) { return 0; }
static int hi_pre_exec(py_handle_t* py) { return 0; }
static void hi_print(py_handle_t* py) { }

#endif /* hi */


#if 1 /* hix */

static const char* hix_py = TEST_LINE("print('hi ' + str(_x))");

static void hix_c(py_handle_t* py) {}

static int hix_pre_compile(py_handle_t* py)
{
  py_create_scalar(py, "_x", PY_FLAG_INT | PY_FLAG_IN);
  return 0;
}

static int hix_pre_exec(py_handle_t* py)
{
  py->vars[0]->in.i[0] = 42;
  return 0;
}

static void hix_print(py_handle_t* py) {}

#endif /* hix */


#if 0 /* arr */

static const char* arr_py = \
TEST_LINE("def fu(x):") \
TEST_LINE(" n = int(3)") \
TEST_LINE(" for i in range(0, n):") \
TEST_LINE("  x[i] = 2048.0") \
TEST_LINE("fu(_arr)");
/* TEST_LINE("j = int(0)")	\ */
/* TEST_LINE("_arr[j] = 2048.0") \ */
/* TEST_LINE("j = j + int(1)") \ */
/* TEST_LINE("_arr[j] = 2048.0") \ */
/* TEST_LINE("kk = int(0)") \ */
/* TEST_LINE("_arr[kk] = 2048.0"); */
/* TEST_LINE("for i in [ int(0) ]:")	\ */
/* TEST_LINE(" print('arr ' + str(_arr[i]))")	\ */
/* TEST_LINE(" print('arr2 ' + str(_arr2[0]))")	\ */
/* TEST_LINE(" _arr[i] *= -1"); */

static void arr_c(py_handle_t* py) {}

static void arr_add_vars(py_handle_t* py)
{
  py_var_t* v;

  v = py_create_array(t, "_arr", 4, PY_FLAG_FLOAT | PY_FLAG_INOUT);
  v->in.f[0] = 42.22;
  v->in.f[1] = 43.33;
  v->in.f[2] = 44.44;
  v->in.f[3] = 45.55;

#if 0
  v = py_create_array(t, "_arr2", 1, PY_FLAG_FLOAT | PY_FLAG_INOUT);
  v->in.f[0] = 42.22;
#endif
}

static void arr_post(py_handle_t* py)
{
  size_t i;
  for (i = 0; i != 4; ++i) printf(" %lf", py->vars[0]->out.py.f[i]);
  printf("\n");
}

#endif /* arr */


/* timing */

#include <math.h>
#include <sys/time.h>

static uint64_t get_cpu_hz(void);
static inline uint64_t rdtsc(void)
{
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return tm.tv_sec * 1000000 + tm.tv_usec;
}

static inline uint64_t sub_ticks(uint64_t a, uint64_t b)
{
  if (a > b) return UINT64_MAX - a + b;
  return b - a;
}

static uint64_t get_cpu_hz(void)
{
  /* return the cpu freq, in mhz */

  static uint64_t cpu_hz = 0;
  unsigned int i;
  unsigned int n;
  struct timeval tms[3];
  uint64_t ticks[2];
  uint64_t all_ticks[10];

  if (cpu_hz != 0) return cpu_hz;

  n = sizeof(all_ticks) / sizeof(all_ticks[0]);
  for (i = 0; i < n; ++i)
  {
    gettimeofday(&tms[0], NULL);
    ticks[0] = rdtsc();
    while (1)
    {
      gettimeofday(&tms[1], NULL);
      timersub(&tms[1], &tms[0], &tms[2]);

      if (tms[2].tv_usec >= 9998)
      {
	ticks[1] = rdtsc();
	all_ticks[i] = sub_ticks(ticks[0], ticks[1]);
	break ;
      }
    }
  }

  cpu_hz = 0;
  for (i = 0; i < n; ++i) cpu_hz += all_ticks[i];
  cpu_hz *= 10;

  return cpu_hz;
}

static inline uint64_t ticks_to_us(uint64_t ticks)
{
  return (ticks * 1000000) / get_cpu_hz();
}


/* main */

int main(int ac, const char** av)
{
  const struct test_desc tests[] =
  {
    /* TEST_DESC(hi), */
    /* TEST_DESC(hix), */
    TEST_DESC(matmul),
    /* TEST_DESC(arr), */
    TEST_DESC_INVALID
  };

  static const size_t n = 10000;

  size_t i;
  size_t j;
  uint64_t ticks[2];
  double us[2];

  if (py_init())
  {
    printf("py_init() error\n");
    return -1;
  }

  for (i = 0; tests[i].name != NULL; ++i)
  {
    const struct test_desc* const t = &tests[i];
    py_handle_t py;

    printf("---");
    printf("--- %s test\n", t->name);

    if (py_open(&py))
    {
      printf("py_open error\n");
      continue ;
    }

    /* add variables */
    if (t->pre_compile(&py))
    {
      printf("py_post_compile error\n");
      goto on_close;
    }

    if (py_compile(&py, t->py_str))
    {
      printf("py_compile error\n");
      goto on_close;
    }

    /* execute python version */
    ticks[0] = rdtsc();
    for (j = 0; j != n; ++j)
    {
      t->pre_exec(&py);

      if (py_execute(&py))
      {
	printf("py_execute error (%zu)\n", j);
	goto on_close;
      }
    }
    ticks[1] = rdtsc();
    us[0] = (double)ticks_to_us(sub_ticks(ticks[0], ticks[1])) / 1000000.0;

    t->print(&py);

    /* execute c version */
    ticks[0] = rdtsc();
    for (j = 0; j != n; ++j)
    {
      t->pre_exec(&py);
      t->c(&py);
    }
    ticks[1] = rdtsc();
    us[1] = (double)ticks_to_us(sub_ticks(ticks[0], ticks[1])) / 1000000.0;
    t->print(&py);

    printf("py = %lfus, c = %lfus\n", us[0], us[1]);

  on_close:
    py_close(&py);
  }

  py_fini();

  return 0;
}
