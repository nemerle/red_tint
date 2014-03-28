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
enum eStringFlags {
    MRB_STR_SHARED = 1,
    MRB_STR_NOFREE = 2
};
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
    static RString *create(mrb_state *mrb, const char *p) { return create(mrb,p,strlen(p));}
    static RString *create(mrb_state *mrb, mrb_int capa);
    static RString *create_static(mrb_state *mrb, const char *p, mrb_int len);
    RString *dup() const {
        return create(m_vm, m_ptr, len);
    }
    void str_cat(const char *m_ptr, int len);
    void str_cat(RString *oth);
    void buf_append(mrb_value str2);
    void str_append(mrb_value str2);
    void str_buf_cat(const char *ptr) { str_buf_cat(ptr,strlen(ptr)); }
    void str_buf_cat(const char *m_ptr, size_t len);
    void str_modify();
    void resize(mrb_int len);
    RString *mrb_str_dump();
    mrb_int mrb_str_to_inum(int base, int badcheck);
    double to_dbl(int badcheck);
    bool capitalize_bang();
    bool chop_bang();
    bool chomp_bang(RString *sep);
    bool downcase_bang();
    bool upcase_bang();
    RString *subseq(mrb_int beg, mrb_int len);
    RString *substr(mrb_int beg, mrb_int len);
private:
};
#define str_new_lit(mrb, lit) RString::create(mrb, (lit), sizeof(lit) - 1)
#define RSTRING(s)        ((RString*)((s).value.p))
#define RSTRING_PTR(s)    (RSTRING(s)->m_ptr)
#define RSTRING_LEN(s)    (RSTRING(s)->len)
#define RSTRING_END(s)    (RSTRING(s)->m_ptr + RSTRING(s)->len)

void mrb_gc_free_str(mrb_state*, RString*);
void mrb_str_concat(mrb_state*, mrb_value, mrb_value);
mrb_value mrb_str_plus(mrb_state*, mrb_value, mrb_value);
RString *mrb_ptr_to_str(mrb_state *, void *);
RString *mrb_obj_as_string(mrb_state *mrb, mrb_value obj);
mrb_value mrb_check_string_type(mrb_state *mrb, mrb_value str);
char *mrb_string_value_cstr(mrb_state *mrb, const RString *ptr);
char *mrb_string_value_ptr(mrb_state *mrb, mrb_value ptr);
mrb_value mrb_str_pool(mrb_state *mrb, RString *str); /* mrb_str_dup */
mrb_value mrb_str_intern(mrb_state *mrb, mrb_value self);
#define mrb_str_cat_lit(mrb, str, lit) mrb_str_cat(mrb, str, (lit), sizeof(lit) - 1)
double mrb_str_to_dbl(mrb_state *mrb, RString *str, int badcheck);
mrb_value mrb_str_to_str(mrb_state *mrb, mrb_value str);
mrb_value mrb_str_inspect(mrb_state *mrb, mrb_value str);
int mrb_str_equal(mrb_state *mrb, mrb_value str1, mrb_value str2);
int mrb_str_cmp(mrb_state *mrb, mrb_value str1, mrb_value str2);
char *mrb_str_to_cstr(mrb_state *mrb, mrb_value str);
