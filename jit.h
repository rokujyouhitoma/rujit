/**********************************************************************

  jit.h -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#ifndef RUBY_JIT_H
#define RUBY_JIT_H 1

extern VALUE *rb_jit_trace(rb_thread_t *, rb_control_frame_t *, VALUE *);
extern void rb_jit_check_redefinition_opt_method(const rb_method_entry_t *me, VALUE klass);

struct rb_vm_global_state {
    rb_serial_t *_global_method_state;
    rb_serial_t *_global_constant_state;
    rb_serial_t *_class_serial;
    short *_ruby_vm_redefined_flag;
};

typedef struct jit_runtime_struct {
    VALUE cArray;
    VALUE cFixnum;
    VALUE cFloat;
    VALUE cHash;
    VALUE cMath;
    VALUE cRegexp;
    VALUE cString;
    VALUE cTime;
    VALUE cSymbol;

    VALUE cFalseClass;
    VALUE cTrueClass;
    VALUE cNilClass;

    // global data
    short *redefined_flag;
    rb_serial_t *global_method_state;
    rb_serial_t *global_constant_state;
    rb_serial_t *class_serial;
    struct rb_vm_struct *_ruby_current_vm;

    // ruby API
    // type check API
    VALUE (*_rb_check_array_type)(VALUE);
    VALUE (*_rb_big_plus)(VALUE, VALUE);
    VALUE (*_rb_big_minus)(VALUE, VALUE);
    VALUE (*_rb_big_mul)(VALUE, VALUE);
    VALUE (*_rb_int2big)(SIGNED_VALUE);
    VALUE (*_rb_str_length)(VALUE);
    VALUE (*_rb_str_plus)(VALUE, VALUE);
    VALUE (*_rb_str_append)(VALUE, VALUE);
    VALUE (*_rb_str_resurrect)(VALUE);
    VALUE (*_rb_range_new)(VALUE, VALUE, int);
    VALUE (*_rb_hash_new)();
    VALUE (*_rb_hash_aref)(VALUE, VALUE);
    VALUE (*_rb_hash_aset)(VALUE, VALUE, VALUE);
    VALUE (*_rb_reg_match)(VALUE, VALUE);
    VALUE (*_rb_reg_new_ary)(VALUE, int);
    VALUE (*_rb_ary_new)();
    VALUE (*_rb_ary_new_from_values)(long n, const VALUE *elts);
    VALUE (*_rb_gvar_get)(struct rb_global_entry *);
    VALUE (*_rb_gvar_set)(struct rb_global_entry *, VALUE);
    VALUE (*_rb_class_new_instance)(int argc, const VALUE *argv, VALUE klass);
    VALUE (*_rb_obj_as_string)(VALUE);

    // Internal ruby APIs
    double (*_ruby_float_mod)(double, double);
    VALUE (*_rb_float_new_in_heap)(double);
    VALUE (*_rb_ary_entry)(VALUE, long);
    void (*_rb_ary_store)(VALUE, long, VALUE);
    void (*_rb_gc_writebarrier)(VALUE a, VALUE b);
#if SIZEOF_INT < SIZEOF_VALUE
    NORETURN(void (*_rb_out_of_int)(SIGNED_VALUE num));
#endif
    NORETURN(void (*_rb_exc_raise)(VALUE));
    VALUE (*_make_no_method_exception)(VALUE exc, const char *format, VALUE obj,
                                       int argc, const VALUE *argv);
} jit_runtime_t;

extern jit_runtime_t jit_runtime;

enum JIT_BOP {
    JIT_BOP_PLUS = BOP_PLUS,
    JIT_BOP_MINUS = BOP_MINUS,
    JIT_BOP_MULT = BOP_MULT,
    JIT_BOP_DIV = BOP_DIV,
    JIT_BOP_MOD = BOP_MOD,
    JIT_BOP_EQ = BOP_EQ,
    JIT_BOP_EQQ = BOP_EQQ,
    JIT_BOP_LT = BOP_LT,
    JIT_BOP_LE = BOP_LE,
    JIT_BOP_LTLT = BOP_LTLT,
    JIT_BOP_AREF = BOP_AREF,
    JIT_BOP_ASET = BOP_ASET,
    JIT_BOP_LENGTH = BOP_LENGTH,
    JIT_BOP_SIZE = BOP_SIZE,
    JIT_BOP_EMPTY_P = BOP_EMPTY_P,
    JIT_BOP_SUCC = BOP_SUCC,
    JIT_BOP_GT = BOP_GT,
    JIT_BOP_GE = BOP_GE,
    JIT_BOP_NOT = BOP_NOT,
    JIT_BOP_NEQ = BOP_NEQ,
    JIT_BOP_MATCH = BOP_MATCH,
    JIT_BOP_FREEZE = BOP_FREEZE,

    JIT_BOP_LAST_ = BOP_LAST_,
    JIT_BOP_AND,
    JIT_BOP_OR,
    JIT_BOP_XOR,
    JIT_BOP_INV,
    JIT_BOP_RSHIFT,
    JIT_BOP_POW,
    JIT_BOP_NEG,

    JIT_BOP_TO_F,
    JIT_BOP_TO_I,
    JIT_BOP_TO_S,

    JIT_BOP_SIN,
    JIT_BOP_COS,
    JIT_BOP_TAN,
    JIT_BOP_LOG2,
    JIT_BOP_LOG10,
    JIT_BOP_EXP,
    JIT_BOP_SQRT,
    JIT_BOP_EXT_LAST_
};

typedef struct trace_side_exit_handler trace_side_exit_handler_t;

struct trace_side_exit_handler {
    struct jit_trace *this_trace;
    struct jit_trace *child_trace;
    VALUE *exit_pc;
    long exit_status;
};

#ifdef JIT_HOST
#define JIT_RUNTIME (&jit_runtime)
#else
static const jit_runtime_t *local_jit_runtime;
#define JIT_RUNTIME (local_jit_runtime)
#endif
#define JIT_OP_UNREDEFINED_P(op, klass) (LIKELY((JIT_RUNTIME->redefined_flag[(op)] & (klass)) == 0))
#endif /* end of include guard */
