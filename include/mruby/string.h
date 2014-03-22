/*
** mruby/string.h - String class
**
** See Copyright Notice in mruby.h
*/
#pragma once

#include "mrbconf.h"
#include "mruby/value.h"

#ifndef MRB_STR_BUF_MIN_SIZE
# define MRB_STR_BUF_MIN_SIZE 128
#endif

#define IS_EVSTR(p,e) ((p) < (e) && (*(p) == '$' || *(p) == '@' || *(p) == '{'))

extern const char mrb_digitmap[];

struct RString : public RBasic {
    static const mrb_vtype ttype=MRB_TT_STRING;
    mrb_int len;
    union {
        mrb_int capa;
        struct mrb_shared_string *shared;
    } aux;
    char *m_ptr;
public:
    static RString *create(mrb_state *mrb, const char *p, mrb_int len);
    static RString *create(mrb_state *mrb, mrb_int capa);

    void str_cat(const char *m_ptr, int len);
    void buf_append(mrb_value str2);
    void str_append(mrb_value str2);
    void str_buf_cat(const char *ptr) { str_buf_cat(ptr,strlen(ptr)); }
    void str_buf_cat(const char *m_ptr, size_t len);
    void str_modify();
private:
};

#define mrb_str_ptr(s)    ((RString*)((s).value.p))
#define RSTRING(s)        ((RString*)((s).value.p))
#define RSTRING_PTR(s)    (RSTRING(s)->m_ptr)
#define RSTRING_LEN(s)    (RSTRING(s)->len)
#define RSTRING_CAPA(s)   (RSTRING(s)->aux.capa)
#define RSTRING_END(s)    (RSTRING(s)->m_ptr + RSTRING(s)->len)

void mrb_gc_free_str(mrb_state*, RString*);
mrb_value mrb_str_literal(mrb_state*, mrb_value);
void mrb_str_concat(mrb_state*, mrb_value, mrb_value);
mrb_value mrb_str_plus(mrb_state*, mrb_value, mrb_value);
mrb_value mrb_ptr_to_str(mrb_state *, void *);
mrb_value mrb_obj_as_string(mrb_state *mrb, mrb_value obj);
mrb_value mrb_str_resize(mrb_state *mrb, mrb_value str, mrb_int len); /* mrb_str_resize */
mrb_value mrb_str_substr(mrb_state *mrb, mrb_value str, mrb_int beg, mrb_int len);
mrb_value mrb_check_string_type(mrb_state *mrb, mrb_value str);
mrb_value mrb_str_buf_new(mrb_state *mrb, mrb_int capa);
mrb_value mrb_str_buf_cat(mrb_value str, const char *ptr, size_t len);

char *mrb_string_value_cstr(mrb_state *mrb, mrb_value *ptr);
char *mrb_string_value_ptr(mrb_state *mrb, mrb_value ptr);
int mrb_str_offset(mrb_state *mrb, mrb_value str, int pos);
mrb_value mrb_str_dup(mrb_state *mrb, mrb_value str); /* mrb_str_dup */
mrb_value mrb_str_dup_static(mrb_state *mrb, mrb_value str); /* mrb_str_dup */
mrb_value mrb_str_intern(mrb_state *mrb, mrb_value self);
mrb_value mrb_str_cat_cstr(mrb_state *, mrb_value, const char *);
mrb_value mrb_str_to_inum(mrb_state *mrb, mrb_value str, int base, int badcheck);
double mrb_str_to_dbl(mrb_state *mrb, mrb_value str, int badcheck);
mrb_value mrb_str_to_str(mrb_state *mrb, mrb_value str);
mrb_int mrb_str_hash(mrb_state *mrb, mrb_value str);
mrb_value mrb_str_buf_append(mrb_state *mrb, mrb_value str, mrb_value str2);
mrb_value mrb_str_inspect(mrb_state *mrb, mrb_value str);
int mrb_str_equal(mrb_state *mrb, mrb_value str1, mrb_value str2);
mrb_value mrb_str_dump(mrb_state *mrb, mrb_value str);
mrb_value mrb_str_cat(mrb_state *mrb, mrb_value str, const char *ptr, int len);
mrb_value mrb_str_append(mrb_state *mrb, mrb_value str, mrb_value str2);

int mrb_str_cmp(mrb_state *mrb, mrb_value str1, mrb_value str2);
char *mrb_str_to_cstr(mrb_state *mrb, mrb_value str);

/* For backward compatibility */
static inline mrb_value
mrb_str_cat2(mrb_state *mrb, mrb_value str, const char *ptr) {
    return mrb_str_cat_cstr(mrb, str, ptr);
}
