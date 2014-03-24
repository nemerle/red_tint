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

static void mt_state_free(mrb_state *mrb, void *p)
{
    mrb->gc()._free(p);
}

static const struct mrb_data_type mt_state_type = {
    MT_STATE_KEY, mt_state_free,
};

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
    if (seed.is_nil()) {
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
    if (seed.is_nil()) {
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

    if (!arg.is_nil()) {
        if (!arg.is_fixnum()) {
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
    if (seed.is_nil()) {
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
    mt_state *t;

    DATA_TYPE(self) = &mt_state_type;
    DATA_PTR(self) = NULL;

    /* avoid memory leaks */
    t = (mt_state*)DATA_PTR(self);
    if (t) {
        mrb->gc()._free(t);
    }
    t = (mt_state *)mrb->gc()._malloc(sizeof(mt_state));
    t->mti = N + 1;

    seed = get_opt(mrb);
    seed = mrb_random_mt_srand(mrb, t, seed);
    mrb_iv_set(mrb, self, mrb->intern2(INSTANCE_RAND_SEED_KEY,INSTANCE_RAND_SEED_KEY_CSTR_LEN), seed);
    DATA_PTR(self) = t;
    return self;
}
static void
mrb_random_rand_seed(mrb_state *mrb, mrb_value self)
{
    mrb_value seed;
    mt_state *t = (mt_state *)DATA_PTR(self);

    seed = mrb_iv_get(self, mrb->intern2(INSTANCE_RAND_SEED_KEY, INSTANCE_RAND_SEED_KEY_CSTR_LEN));
    if (seed.is_nil()) {
        mrb_random_mt_srand(mrb, t, mrb_nil_value());
    }
}
static mrb_value mrb_random_rand(mrb_state *mrb, mrb_value self)
{
    mrb_value max;
    mt_state *t = (mt_state *)DATA_PTR(self);
    max = get_opt(mrb);
    mrb_random_rand_seed(mrb, self);
    return mrb_random_mt_rand(mrb, t, max);
}

static mrb_value mrb_random_srand(mrb_state *mrb, mrb_value self)
{
    mrb_value seed;
    mrb_value old_seed;
    mt_state *t = (mt_state *)DATA_PTR(self);

    seed = get_opt(mrb);
    seed = mrb_random_mt_srand(mrb, t, seed);
    mrb_sym sym_seed_key = mrb->intern2(INSTANCE_RAND_SEED_KEY,INSTANCE_RAND_SEED_KEY_CSTR_LEN);
    old_seed = mrb_iv_get(self, sym_seed_key);
    mrb_iv_set(mrb, self, sym_seed_key, seed);
    return old_seed;
}
static void
mrb_random_g_rand_seed(mrb_state *mrb)
{
    mrb_value seed;

    seed = mrb_gv_get(mrb, mrb->intern2(GLOBAL_RAND_SEED_KEY, GLOBAL_RAND_SEED_KEY_CSTR_LEN));
    if (seed.is_nil()) {
        mrb_random_mt_g_srand(mrb, mrb_nil_value());
    }
}
/*
 *  call-seq:
 *     ary.shuffle!   ->   ary
 *
 *  Shuffles elements in self in place.
 */

static mrb_value
mrb_ary_shuffle_bang(mrb_state *mrb, mrb_value ary)
{
    mrb_int i;
    mrb_value seed;
    RArray *arr_p = mrb_ary_ptr(ary);
    mrb_value random = mrb_nil_value();
    if (arr_p->m_len > 1) {
        mrb_get_args(mrb, "|o", &random);
        if( random.is_nil() ) {
            mrb_random_g_rand_seed(mrb);
        } else {
            mrb_data_check_type(mrb, random, &mt_state_type);
            mrb_random_rand_seed(mrb, random);
        }
        arr_p->mrb_ary_modify();
        for (i = arr_p->m_len - 1; i > 0; i--)  {
            mrb_int j;
            if( random.is_nil() ) {
                j = mrb_fixnum(mrb_random_mt_g_rand(mrb, mrb_fixnum_value(arr_p->m_len)));
            } else {
                j = mrb_fixnum(mrb_random_mt_rand(mrb, (mt_state *)DATA_PTR(random), mrb_fixnum_value(arr_p->m_len)));
            }
            std::swap(arr_p->m_ptr[i],arr_p->m_ptr[j]);
        }
    }

    return ary;
}

/*
 *  call-seq:
 *     ary.shuffle   ->   new_ary
 *
 *  Returns a new array with elements of self shuffled.
 */

static mrb_value
mrb_ary_shuffle(mrb_state *mrb, mrb_value ary)
{
    mrb_value new_ary = RArray::new_from_values(mrb, RARRAY_LEN(ary), RARRAY_PTR(ary));
    mrb_ary_shuffle_bang(mrb, new_ary);

    return new_ary;
}

void mrb_mruby_random_gem_init(mrb_state *mrb)
{
    mrb->kernel_module->define_method("rand", mrb_random_g_rand, MRB_ARGS_OPT(1)).
            define_method("srand", mrb_random_g_srand, MRB_ARGS_OPT(1));

    mrb->define_class("Random", mrb->object_class)
            .instance_tt(MRB_TT_DATA)
            .define_class_method("rand", mrb_random_g_rand, MRB_ARGS_OPT(1))
            .define_class_method("srand", mrb_random_g_srand, MRB_ARGS_OPT(1))
            .define_method("initialize", mrb_random_init, MRB_ARGS_OPT(1))
            .define_method("rand", mrb_random_rand, MRB_ARGS_OPT(1))
            .define_method("srand", mrb_random_srand, MRB_ARGS_OPT(1))
            .define_method("shuffle", mrb_ary_shuffle, MRB_ARGS_OPT(1))
            .define_method("shuffle!", mrb_ary_shuffle_bang, MRB_ARGS_OPT(1))
            .fin()
            ;
}

void mrb_mruby_random_gem_final(mrb_state *mrb)
{
}

