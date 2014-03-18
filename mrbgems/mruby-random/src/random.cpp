/*
** random.c - Random module
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/variable.h"
#include "mruby/data.h"
#include "mruby/class.h"
#include "mt19937ar.h"

#include <time.h>

#define GLOBAL_RAND_SEED_KEY          "$mrb_g_rand_seed"
#define GLOBAL_RAND_SEED_KEY_CSTR_LEN 16
#define INSTANCE_RAND_SEED_KEY          "$mrb_i_rand_seed"
#define INSTANCE_RAND_SEED_KEY_CSTR_LEN 16
#define MT_STATE_KEY            "$mrb_i_mt_state"
#define MT_STATE_KEY_CSTR_LEN 15

static void mt_state_free(mrb_state *mrb, void *p)
{
    mrb->gc()._free(p);
}

static const struct mrb_data_type mt_state_type = {
    MT_STATE_KEY, mt_state_free,
};

static mt_state *mrb_mt_get_context(mrb_state *mrb,  mrb_value self)
{
    mt_state *t;
    mrb_value context;

    context = mrb_iv_get(self, mrb->intern2(MT_STATE_KEY,MT_STATE_KEY_CSTR_LEN));

    t = (mt_state *)mrb_data_get_ptr(mrb, context, &mt_state_type);
    if (!t)
        mrb->mrb_raise(E_RUNTIME_ERROR, "mt_state get from mrb_iv_get failed");

    return t;
}

static void mt_g_srand(unsigned long seed)
{
    init_genrand(seed);
}

static unsigned long mt_g_rand()
{
    return genrand_int32();
}

static double mt_g_rand_real()
{
    return genrand_real1();
}

static mrb_value mrb_random_mt_g_srand(mrb_state *mrb, mrb_value seed)
{
    if (mrb_nil_p(seed)) {
        seed = mrb_fixnum_value(time(NULL) + mt_g_rand());
        if (mrb_fixnum(seed) < 0) {
            seed = mrb_fixnum_value( 0 - mrb_fixnum(seed));
        }
    }

    mt_g_srand((unsigned) mrb_fixnum(seed));

    return seed;
}

static mrb_value mrb_random_mt_g_rand(mrb_state *mrb, mrb_value max)
{
    mrb_value value;

    if (mrb_fixnum(max) == 0) {
        value = mrb_float_value(mt_g_rand_real());
    } else {
        value = mrb_fixnum_value(mt_g_rand() % mrb_fixnum(max));
    }

    return value;
}

static void mt_srand(mt_state *t, unsigned long seed)
{
    mrb_random_init_genrand(t, seed);
}

static unsigned long mt_rand(mt_state *t)
{
    return mrb_random_genrand_int32(t);
}

static double mt_rand_real(mt_state *t)
{
    return mrb_random_genrand_real1(t);
}

static mrb_value mrb_random_mt_srand(mrb_state *mrb, mt_state *t, mrb_value seed)
{
    if (mrb_nil_p(seed)) {
        seed = mrb_fixnum_value(time(NULL) + mt_rand(t));
        if (mrb_fixnum(seed) < 0) {
            seed = mrb_fixnum_value( 0 - mrb_fixnum(seed));
        }
    }

    mt_srand(t, (unsigned) mrb_fixnum(seed));

    return seed;
}

static mrb_value mrb_random_mt_rand(mrb_state *mrb, mt_state *t, mrb_value max)
{
    mrb_value value;

    if (mrb_fixnum(max) == 0) {
        value = mrb_float_value(mt_rand_real(t));
    } else {
        value = mrb_fixnum_value(mt_rand(t) % mrb_fixnum(max));
    }

    return value;
}

static mrb_value get_opt(mrb_state* mrb)
{
    mrb_value arg = mrb_fixnum_value(0);
    mrb_get_args(mrb, "|o", &arg);

    if (!mrb_nil_p(arg)) {
        if (!mrb_fixnum_p(arg)) {
            mrb->mrb_raise(E_ARGUMENT_ERROR, "invalid argument type");
        }
        arg = mrb_check_convert_type(mrb, arg, MRB_TT_FIXNUM, "Fixnum", "to_int");
        if (mrb_fixnum(arg) < 0) {
            arg = mrb_fixnum_value(0 - mrb_fixnum(arg));
        }
    }
    return arg;
}

static mrb_value mrb_random_g_rand(mrb_state *mrb, mrb_value self)
{
    mrb_value max;
    mrb_value seed;

    max = get_opt(mrb);
    seed = mrb_gv_get(mrb, mrb->intern2(GLOBAL_RAND_SEED_KEY,GLOBAL_RAND_SEED_KEY_CSTR_LEN));
    if (mrb_nil_p(seed)) {
        mrb_random_mt_g_srand(mrb, mrb_nil_value());
    }
    return mrb_random_mt_g_rand(mrb, max);
}

static mrb_value mrb_random_g_srand(mrb_state *mrb, mrb_value self)
{
    mrb_value seed;
    mrb_value old_seed;

    seed = get_opt(mrb);
    seed = mrb_random_mt_g_srand(mrb, seed);
    mrb_sym glob_randseed=mrb->intern2(GLOBAL_RAND_SEED_KEY,GLOBAL_RAND_SEED_KEY_CSTR_LEN);
    old_seed = mrb_gv_get(mrb,glob_randseed);
    mrb_gv_set(mrb, glob_randseed, seed);
    return old_seed;
}

static mrb_value mrb_random_init(mrb_state *mrb, mrb_value self)
{
    mrb_value seed;


    mt_state *t = (mt_state *)mrb->gc()._malloc(sizeof(mt_state));
    t->mti = N + 1;

    seed = get_opt(mrb);
    seed = mrb_random_mt_srand(mrb, t, seed);
    mrb_iv_set(mrb, self, mrb->intern2(INSTANCE_RAND_SEED_KEY,INSTANCE_RAND_SEED_KEY_CSTR_LEN), seed);
    mrb_iv_set(mrb, self, mrb->intern2(MT_STATE_KEY,MT_STATE_KEY_CSTR_LEN),
               mrb_obj_value(Data_Wrap_Struct(mrb, mrb->object_class, &mt_state_type, (void*) t)));
    return self;
}

static mrb_value mrb_random_rand(mrb_state *mrb, mrb_value self)
{
    mrb_value max;
    mrb_value seed;
    mt_state *t = mrb_mt_get_context(mrb, self);

    max = get_opt(mrb);
    seed = mrb_iv_get(self, mrb->intern2(INSTANCE_RAND_SEED_KEY,INSTANCE_RAND_SEED_KEY_CSTR_LEN));
    if (mrb_nil_p(seed)) {
        mrb_random_mt_srand(mrb, t, mrb_nil_value());
    }
    return mrb_random_mt_rand(mrb, t, max);
}

static mrb_value mrb_random_srand(mrb_state *mrb, mrb_value self)
{
    mrb_value seed;
    mrb_value old_seed;
    mt_state *t = mrb_mt_get_context(mrb, self);

    seed = get_opt(mrb);
    seed = mrb_random_mt_srand(mrb, t, seed);
    mrb_sym sym_seed_key = mrb->intern2(INSTANCE_RAND_SEED_KEY,INSTANCE_RAND_SEED_KEY_CSTR_LEN);
    old_seed = mrb_iv_get(self, sym_seed_key);
    mrb_iv_set(mrb, self, sym_seed_key, seed);
    return old_seed;
}

void mrb_mruby_random_gem_init(mrb_state *mrb)
{
    mrb->kernel_module->define_method("rand", mrb_random_g_rand, MRB_ARGS_OPT(1)).
        define_method("srand", mrb_random_g_srand, MRB_ARGS_OPT(1));

    mrb->define_class("Random", mrb->object_class)
        .define_class_method("rand", mrb_random_g_rand, MRB_ARGS_OPT(1))
        .define_class_method("srand", mrb_random_g_srand, MRB_ARGS_OPT(1))
        .define_method("initialize", mrb_random_init, MRB_ARGS_OPT(1))
        .define_method("rand", mrb_random_rand, MRB_ARGS_OPT(1))
        .define_method("srand", mrb_random_srand, MRB_ARGS_OPT(1))
    ;
}

void mrb_mruby_random_gem_final(mrb_state *mrb)
{
}

