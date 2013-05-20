/*
** mrbconf.h - mruby core configuration
**
** See Copyright Notice in mruby.h
*/

#pragma once
#ifndef __cplusplus
# error "Only c++ for now"
#endif

#include <stdint.h>
#include <stddef.h>

/* configuration options: */
/* add -DMRB_USE_FLOAT to use float instead of double for floating point numbers */
//#define MRB_USE_FLOAT

/* add -DMRB_INT64 to use 64bit integer for mrb_int */
//#define MRB_INT64

/* represent mrb_value in boxed double; conflict with MRB_USE_FLOAT */
//#define MRB_NAN_BOXING

/* define on big endian machines; used by MRB_NAN_BOXING */
//#define MRB_ENDIAN_BIG

/* argv max size in mrb_funcall */
//#define MRB_FUNCALL_ARGC_MAX 16

/* number of object per heap page */
//#define MRB_HEAP_PAGE_SIZE 1024

/* use segmented list for IV table */
//#define MRB_USE_IV_SEGLIST

/* initial size for IV khash; ignored when MRB_USE_IV_SEGLIST is set */
//#define MRB_IVHASH_INIT_SIZE 8

/* initial size for IREP array */
//#define MRB_IREP_ARRAY_INIT_SIZE (256u)

/* default size of khash table bucket */
//#define KHASH_DEFAULT_SIZE 32

/* allocated memory address alignment */
//#define POOL_ALIGNMENT 4

/* page size of memory pool */
//#define POOL_PAGE_SIZE 16000

/* initial minimum size for string buffer */
//#define MRB_STR_BUF_MIN_SIZE 128

/* array size for parser buffer */
//#define MRB_PARSER_BUF_SIZE 1024

/* -DDISABLE_XXXX to drop following features */
//#define DISABLE_STDIO		/* use of stdio */

/* -DENABLE_XXXX to enable following features */
//#define ENABLE_DEBUG		/* hooks for debugger */

/* end of configuration */

#ifdef MRB_USE_FLOAT
  typedef float mrb_float;
# define mrb_float_to_str(buf, i) sprintf(buf, "%.7e", i)
# define str_to_mrb_float(buf) strtof(buf, NULL)
#else
  typedef double mrb_float;
# define mrb_float_to_str(buf, i) sprintf(buf, "%.16e", i)
# define str_to_mrb_float(buf) strtod(buf, NULL)
#endif

#if defined(MRB_INT64)
# ifdef MRB_NAN_BOXING
#  error Cannot use NaN boxing when mrb_int is 64bit
# else
   typedef int64_t mrb_int;
#  define MRB_INT_MIN INT64_MIN
#  define MRB_INT_MAX INT64_MAX
#  define PRIdMRB_INT PRId64
#  define PRIiMRB_INT PRIi64
#  define PRIoMRB_INT PRIo64
#  define PRIxMRB_INT PRIx64
#  define PRIXMRB_INT PRIX64
# endif
#elif defined(MRB_INT16)
  typedef int16_t mrb_int;
# define MRB_INT_MIN INT16_MIN
# define MRB_INT_MAX INT16_MAX
#else
  typedef int32_t mrb_int;
# define MRB_INT_MIN INT32_MIN
# define MRB_INT_MAX INT32_MAX
# define PRIdMRB_INT PRId32
# define PRIiMRB_INT PRIi32
# define PRIoMRB_INT PRIo32
# define PRIxMRB_INT PRIx32
# define PRIXMRB_INT PRIX32
#endif
typedef short mrb_sym;

/* define ENABLE_XXXX from DISABLE_XXX */
#define ENABLE_STDIO 1
#ifndef ENABLE_DEBUG
#define DISABLE_DEBUG
#endif

#include <inttypes.h>
typedef bool mrb_bool;

# include <stdio.h>

