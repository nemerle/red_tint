/*
** etc.c -
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/string.h"
#include "mruby/data.h"
#include "mruby/class.h"

RData* RData::object_alloc(mrb_state *mrb, RClass *klass, void *ptr, const mrb_data_type *type)
{
    RData *data = mrb->gc().obj_alloc<RData>(klass);
    data->data = ptr;
    data->type = type;

    return data;
}

void * mrb_data_get_ptr(mrb_state *, const mrb_value &obj, const mrb_data_type *type)
{
    if (obj.is_special_const() || (mrb_type(obj) != MRB_TT_DATA)) {
        return nullptr;
    }
    if (DATA_TYPE(obj) != type) {
        return nullptr;
    }
    return DATA_PTR(obj);
}
void mrb_data_check_type(mrb_state *mrb, const mrb_value &obj, const mrb_data_type *type)
{
    if (obj.is_special_const() || (mrb_type(obj) != MRB_TT_DATA)) {
        mrb_check_type(mrb, obj, MRB_TT_DATA);
    }
    if (DATA_TYPE(obj) != type) {
        const mrb_data_type *t2 = DATA_TYPE(obj);
        if (t2) {
            mrb->mrb_raisef(E_TYPE_ERROR, "wrong argument type %S (expected %S)",
                       mrb_str_new_cstr(mrb, t2->struct_name), mrb_str_new_cstr(mrb, type->struct_name));
        } else {
            const RClass *c = RClass::mrb_class(mrb, obj);
            mrb->mrb_raisef(E_TYPE_ERROR, "uninitialized %S (expected %S)",
                       mrb_value::wrap((RClass *)c), mrb_str_new_cstr(mrb, type->struct_name));
        }
    }
}
void * mrb_data_check_and_get_ptr(mrb_value obj, const mrb_data_type *type)
{
    if (obj.is_special_const() || (mrb_type(obj) != MRB_TT_DATA)) {
      return nullptr;
    }
    if (DATA_TYPE(obj) != type) {
      return nullptr;
    }
//    mrb_data_check_type(mrb, obj, type);
    return obj.ptr<RData>()->data;
}

mrb_sym mrb_obj_to_sym(mrb_state *mrb, mrb_value name)
{
    mrb_value tmp;
    mrb_sym id;

    switch (mrb_type(name)) {
        default:
            tmp = mrb_check_string_type(mrb, name);
            if (tmp.is_nil()) {
                tmp = mrb_value::wrap(mrb_inspect(mrb, name));
                mrb->mrb_raisef(E_TYPE_ERROR, "%S is not a symbol", tmp);
            }
            name = tmp;
            /* fall through */
        case MRB_TT_STRING:
            name = mrb_str_intern(mrb, name);
            /* fall through */
        case MRB_TT_SYMBOL:
            return mrb_symbol(name);
    }
    return id;
}

static mrb_int float_id(mrb_float f)
{
    const char *p = (const char*)&f;
    int len = sizeof(f);
    mrb_int id = 0;

    while (len--) {
        id = id*65599 + *p;
        p++;
    }
    id = id + (id>>5);

    return id;
}

mrb_int mrb_obj_id(const mrb_value &obj)
{
    mrb_vtype tt = mrb_type(obj);

#define MakeID2(p,t) (((intptr_t)(p))^(t))
#define MakeID(p)    MakeID2(p,tt)

    switch (tt) {
        case  MRB_TT_FREE:
        case  MRB_TT_UNDEF:
            return MakeID(0); /* not define */
        case  MRB_TT_FALSE:
            if (obj.is_nil())
                return MakeID(1);
            return MakeID(0);
        case  MRB_TT_TRUE:
            return MakeID(1);
        case  MRB_TT_SYMBOL:
            return MakeID(mrb_symbol(obj));
        case  MRB_TT_FIXNUM:
            return MakeID2(float_id((mrb_float)mrb_fixnum(obj)), MRB_TT_FLOAT);
        case  MRB_TT_FLOAT:
            return MakeID(float_id(mrb_float(obj)));
        case  MRB_TT_STRING:
        case  MRB_TT_OBJECT:
        case  MRB_TT_CLASS:
        case  MRB_TT_MODULE:
        case  MRB_TT_ICLASS:
        case  MRB_TT_SCLASS:
        case  MRB_TT_PROC:
        case  MRB_TT_ARRAY:
        case  MRB_TT_HASH:
        case  MRB_TT_RANGE:
        case  MRB_TT_EXCEPTION:
        case  MRB_TT_FILE:
        case  MRB_TT_DATA:
        default:
            return MakeID(obj.basic_ptr());
    }
}
