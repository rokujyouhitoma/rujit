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

#endif /* end of include guard */
