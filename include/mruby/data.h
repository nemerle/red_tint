/*
** mruby/data.h - Data class
**
** See Copyright Notice in mruby.h
*/

#pragma once

#include "value.h"
struct RClass;
struct mrb_data_type {
    const char *struct_name;
    void (*dfree)(mrb_state *mrb, void*);
};

struct RData : public RObject {
    static const mrb_vtype ttype=MRB_TT_DATA;
    const mrb_data_type *type;
    void *data;
    static RData *object_alloc(mrb_state *mrb, RClass *klass, void *ptr, const mrb_data_type *type);
};

#define Data_Wrap_Struct(mrb,klass,type,ptr)\
    mrb_data_object_alloc(mrb,klass,ptr,type)

#define Data_Make_Struct(mrb,klass,strct,type,sval,data) do {\
    sval = mrb_malloc(mrb, sizeof(strct)),\
{ static const strct zero = { 0 }; *sval = zero; },\
    data = Data_Wrap_Struct(mrb,klass,type,sval)\
    } while (0)
#define Data_Get_Struct(mrb,obj,type,sval) do {\
    *(void**)&sval = mrb_data_check_and_get(mrb, obj, type); \
    } while (0)

#define RDATA(obj)         ((RData *)((obj).value.p))
#define DATA_PTR(d)        (RDATA(d)->data)
#define DATA_TYPE(d)       (RDATA(d)->type)

//#define mrb_get_datatype(mrb,val,type) mrb_data_get_ptr(mrb, val, type)
void mrb_data_check_type(mrb_state *mrb, const mrb_value &, const mrb_data_type*);
void *mrb_data_get_ptr(mrb_state *mrb, const mrb_value &, const mrb_data_type*);
#define DATA_GET_PTR(mrb,obj,dtype,type) (type*)mrb_data_get_ptr(mrb,obj,dtype)
void *mrb_data_check_and_get(mrb_state *mrb, mrb_value, const mrb_data_type*);
