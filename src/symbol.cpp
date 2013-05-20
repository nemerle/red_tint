/*
** symbol.c - Symbol class
**
** See Copyright Notice in mruby.h
*/

#include <ctype.h>
#include <limits.h>
#include <string.h>
#include "mruby.h"
#include "mruby/khash.h"
#include "mruby/string.h"
#include "mruby/class.h"

namespace {
/* ------------------------------------------------------ */
struct symbol_name {
    size_t len;
    const char *name;
};

struct SymHashFunc {
    inline khint_t operator()(mrb_state *mrb, const symbol_name &s) const
    {
        khint_t h = 0;
        size_t i;
        const char *p = s.name;

        for (i=0; i<s.len; i++) {
            h = (h << 5) - h + *p++;
        }
        return h;
    }
};
struct SymHashEqual {
    khint_t operator()(mrb_state *,const symbol_name &a,const symbol_name &b) const
    {
        return (a.len == b.len && memcmp(a.name, b.name, a.len) == 0);
    }
};

} // end of anonymous namespace
struct SymTable {
    typedef kh_T<symbol_name, mrb_sym,SymHashFunc,SymHashEqual> kh_n2s;
    typedef kh_n2s::iterator iterator;

    SymTable(mrb_state *mrb) {
        m_tab = kh_n2s::init(mrb);
    }
    iterator find(const symbol_name &k) {
        return m_tab->get(k);
    }
    iterator begin() const { return m_tab->begin(); }
    iterator end() const { return m_tab->end(); }
    const mrb_sym &operator[](iterator x) const { return m_tab->value(x);}
    mrb_sym operator[](iterator x) { return m_tab->value(x);}
    const symbol_name &key(iterator x) const { return m_tab->key(x);}
    void insert(const symbol_name &key,mrb_sym v)
    {
        iterator k = m_tab->put(key);
        m_tab->value(k) = v;
    }
    bool exist(iterator x) {
        return m_tab->exist(x);
    }
    void destroy() {
        m_tab->destroy();
    }
protected:
    kh_n2s *m_tab;

};
/* ------------------------------------------------------ */
mrb_sym mrb_intern2(mrb_state *mrb, const char *name, size_t len)
{
    return mrb->intern2(name,len);
}
mrb_sym mrb_state::intern2(const char *name, size_t len)
{
    SymTable &name2sym_tab(*this->name2sym);
    symbol_name sname;
    mrb_sym sym;
    char *p;

    sname.len = len;
    sname.name = name;
    khiter_t k = name2sym_tab.find(sname);
    if (k != name2sym_tab.end())
        return name2sym_tab[k];

    sym = ++this->symidx;
    p = (char *)gc()._malloc(len+1);
    memcpy(p, name, len);
    p[len] = 0;
    sname.name = (const char*)p;
    name2sym_tab.insert(sname,sym);
    return sym;
}

mrb_sym mrb_intern_cstr(mrb_state *mrb, const char *name)
{
    return mrb_intern2(mrb, name, strlen(name));
}

mrb_sym mrb_intern_str(mrb_state *mrb, mrb_value str)
{
    return mrb_intern2(mrb, RSTRING_PTR(str), RSTRING_LEN(str));
}

mrb_value mrb_check_intern_cstr(mrb_state *mrb, const char *name)
{
    return mrb_check_intern(mrb, name, strlen(name));
}

mrb_value mrb_check_intern(mrb_state *mrb, const char *name, size_t len)
{
    SymTable &name2sym_tab(*mrb->name2sym);
    symbol_name sname {len,name};
    auto iter = name2sym_tab.find(sname);

    if(iter != name2sym_tab.end())
        return mrb_symbol_value(name2sym_tab[iter]);
    return mrb_nil_value();
}

mrb_value mrb_check_intern_str(mrb_state *mrb, mrb_value str)
{
    return mrb_check_intern(mrb, RSTRING_PTR(str), RSTRING_LEN(str));
}

/* lenp must be a pointer to a size_t variable */
const char* mrb_sym2name_len(mrb_state *mrb, mrb_sym sym, size_t &lenp)
{
    SymTable &name2sym_tab(*mrb->name2sym);
    for (SymTable::iterator k = name2sym_tab.begin(); k != name2sym_tab.end(); k++) {
        if ( !name2sym_tab.exist(k))
            continue;
        if (name2sym_tab[k] != sym)
            continue;
        symbol_name sname(name2sym_tab.key(k));
        lenp = sname.len;
        return sname.name;
    }
    lenp = 0;
    return nullptr;  /* missing */
}

void mrb_symtbl_free(mrb_state *mrb)
{
    SymTable &h(*mrb->name2sym);

    for (SymTable::iterator k = h.begin(); k != h.end(); k++)
        if (h.exist(k))
            mrb->gc()._free((char *)h.key(k).name);
    h.destroy();
}

void mrb_init_symtbl(mrb_state *mrb)
{
    mrb->name2sym = new(mrb->gc()._malloc(sizeof(SymTable))) SymTable(mrb);
}

/**********************************************************************
 * Document-class: Symbol
 *
 *  <code>Symbol</code> objects represent names and some strings
 *  inside the Ruby
 *  interpreter. They are generated using the <code>:name</code> and
 *  <code>:"string"</code> literals
 *  syntax, and by the various <code>to_sym</code> methods. The same
 *  <code>Symbol</code> object will be created for a given name or string
 *  for the duration of a program's execution, regardless of the context
 *  or meaning of that name. Thus if <code>Fred</code> is a constant in
 *  one context, a method in another, and a class in a third, the
 *  <code>Symbol</code> <code>:Fred</code> will be the same object in
 *  all three contexts.
 *
 *     module One
 *       class Fred
 *       end
 *       $f1 = :Fred
 *     end
 *     module Two
 *       Fred = 1
 *       $f2 = :Fred
 *     end
 *     def Fred()
 *     end
 *     $f3 = :Fred
 *     $f1.object_id   #=> 2514190
 *     $f2.object_id   #=> 2514190
 *     $f3.object_id   #=> 2514190
 *
 */


/* 15.2.11.3.1  */
/*
 *  call-seq:
 *     sym == obj   -> true or false
 *
 *  Equality---If <i>sym</i> and <i>obj</i> are exactly the same
 *  symbol, returns <code>true</code>.
 */

static mrb_value
sym_equal(mrb_state *mrb, mrb_value sym1)
{
    mrb_value sym2;
    mrb_bool equal_p;

    mrb_get_args(mrb, "o", &sym2);
    equal_p = mrb_obj_equal(mrb, sym1, sym2);

    return mrb_bool_value(equal_p);
}

/* 15.2.11.3.2  */
/* 15.2.11.3.3  */
/*
 *  call-seq:
 *     sym.id2name   -> string
 *     sym.to_s      -> string
 *
 *  Returns the name or string corresponding to <i>sym</i>.
 *
 *     :fred.id2name   #=> "fred"
 */
mrb_value
mrb_sym_to_s(mrb_state *mrb, mrb_value sym)
{
    mrb_sym id = mrb_symbol(sym);
    const char *p;
    size_t len;

    p = mrb_sym2name_len(mrb, id, len);
    return mrb_str_new_static(mrb, p, len);
}

/* 15.2.11.3.4  */
/*
 * call-seq:
 *   sym.to_sym   -> sym
 *   sym.intern   -> sym
 *
 * In general, <code>to_sym</code> returns the <code>Symbol</code> corresponding
 * to an object. As <i>sym</i> is already a symbol, <code>self</code> is returned
 * in this case.
 */

static mrb_value
sym_to_sym(mrb_state *mrb, mrb_value sym)
{
    return sym;
}

/* 15.2.11.3.5(x)  */
/*
 *  call-seq:
 *     sym.inspect    -> string
 *
 *  Returns the representation of <i>sym</i> as a symbol literal.
 *
 *     :fred.inspect   #=> ":fred"
 */

#if __STDC__
# define SIGN_EXTEND_CHAR(c) ((signed char)(c))
#else  /* not __STDC__ */
/* As in Harbison and Steele.  */
# define SIGN_EXTEND_CHAR(c) ((((unsigned char)(c)) ^ 128) - 128)
#endif
#define is_identchar(c) (SIGN_EXTEND_CHAR(c)!=-1&&(ISALNUM(c) || (c) == '_'))

static int is_special_global_name(const char* m)
{
    switch (*m) {
    case '~': case '*': case '$': case '?': case '!': case '@':
    case '/': case '\\': case ';': case ',': case '.': case '=':
    case ':': case '<': case '>': case '\"':
    case '&': case '`': case '\'': case '+':
    case '0':
        ++m;
        break;
    case '-':
        ++m;
        if (is_identchar(*m)) m += 1;
        break;
    default:
        if (!ISDIGIT(*m)) return false;
        do ++m; while (ISDIGIT(*m));
        break;
    }
    return !*m;
}

static int symname_p(const char *name)
{
    const char *m = name;
    int localid = false;

    if (!m) return false;
    switch (*m) {
    case '\0':
        return false;

    case '$':
        if (is_special_global_name(++m)) return true;
        goto id;

    case '@':
        if (*++m == '@') ++m;
        goto id;

    case '<':
        switch (*++m) {
        case '<': ++m; break;
        case '=': if (*++m == '>') ++m; break;
        default: break;
        }
        break;

    case '>':
        switch (*++m) {
        case '>': case '=': ++m; break;
        default: break;
        }
        break;

    case '=':
        switch (*++m) {
        case '~': ++m; break;
        case '=': if (*++m == '=') ++m; break;
        default: return false;
        }
        break;

    case '*':
        if (*++m == '*') ++m;
        break;
    case '!':
        if (*++m == '=') ++m;
        break;
    case '+': case '-':
        if (*++m == '@') ++m;
        break;
    case '|':
        if (*++m == '|') ++m;
        break;
    case '&':
        if (*++m == '&') ++m;
        break;

    case '^': case '/': case '%': case '~': case '`':
        ++m;
        break;

    case '[':
        if (*++m != ']') return false;
        if (*++m == '=') ++m;
        break;

    default:
        localid = !ISUPPER(*m);
id:
        if (*m != '_' && !ISALPHA(*m)) return false;
        while (is_identchar(*m)) m += 1;
        if (localid) {
            switch (*m) {
            case '!': case '?': case '=': ++m;
            default: break;
            }
        }
        break;
    }
    return *m ? false : true;
}

static mrb_value sym_inspect(mrb_state *mrb, mrb_value sym)
{
    mrb_value str;
    const char *name;
    size_t len;
    mrb_sym id = mrb_symbol(sym);

    name = mrb_sym2name_len(mrb, id, len);
    str = mrb_str_new(mrb, 0, len+1);
    RSTRING(str)->ptr[0] = ':';
    memcpy(RSTRING(str)->ptr+1, name, len);
    if (!symname_p(name) || strlen(name) != len) {
        str = mrb_str_dump(mrb, str);
        memcpy(RSTRING(str)->ptr, ":\"", 2);
    }
    return str;
}

mrb_value mrb_sym2str(mrb_state *mrb, mrb_sym sym)
{
    size_t len;
    const char *name = mrb_sym2name_len(mrb, sym, len);
    mrb_value str;

    if (!name)
        return mrb_undef_value(); /* can't happen */
    str = mrb_str_new_static(mrb, name, len);
    if (symname_p(name) && strlen(name) == len) {
        return str;
    }
    return mrb_str_dump(mrb, str);
}

const char* mrb_sym2name(mrb_state *mrb, mrb_sym sym) {
    size_t len;
    const char *name = mrb_sym2name_len(mrb, sym, len);

    if (!name) return NULL;
    if (symname_p(name) && strlen(name) == len) {
        return name;
    }
    else {
        mrb_value str = mrb_str_dump(mrb, mrb_str_new_static(mrb, name, len));
        return RSTRING(str)->ptr;
    }
}

static mrb_value sym_cmp(mrb_state *mrb, mrb_value s1) {
    mrb_value s2;

    mrb_get_args(mrb, "o", &s2);
    if (mrb_type(s2) != MRB_TT_SYMBOL)
        return mrb_nil_value();
    mrb_sym sym1 = mrb_symbol(s1);
    mrb_sym sym2 = mrb_symbol(s2);
    if (sym1 == sym2)
        return mrb_fixnum_value(0);
        size_t len, len1, len2;

    const char *p1 = mrb_sym2name_len(mrb, sym1, len1);
    const char *p2 = mrb_sym2name_len(mrb, sym2, len2);
    len = std::min(len1, len2);
    int retval = memcmp(p1, p2, len);
        if (retval == 0) {
            if (len1 == len2) return mrb_fixnum_value(0);
            if (len1 > len2)  return mrb_fixnum_value(1);
            return mrb_fixnum_value(-1);
        }
    if (retval > 0)
        return mrb_fixnum_value(1);
        return mrb_fixnum_value(-1);
}

void mrb_init_symbol(mrb_state *mrb)
{
    mrb->symbol_class = &mrb->define_class("Symbol", mrb->object_class)
        .define_method("===",             sym_equal,               MRB_ARGS_REQ(1))            /* 15.2.11.3.1  */
        .define_method("id2name",         mrb_sym_to_s,            MRB_ARGS_NONE())        /* 15.2.11.3.2  */
        .define_method("to_s",            mrb_sym_to_s,            MRB_ARGS_NONE())        /* 15.2.11.3.3  */
        .define_method("to_sym",          sym_to_sym,              MRB_ARGS_NONE())        /* 15.2.11.3.4  */
        .define_method("inspect",         sym_inspect,             MRB_ARGS_NONE())        /* 15.2.11.3.5(x)  */
        .define_method("<=>",             sym_cmp,                 MRB_ARGS_REQ(1))
            ;
    mrb->init_sym = mrb->intern_cstr("initialize");
}
