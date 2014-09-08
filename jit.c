/**********************************************************************

  jit.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/vm.h"
#include "vm_core.h"
#include "internal.h"
#include "jit_prelude.c"
#include "insns.inc"
#include "insns_info.inc"

static int disable_jit = 0;

typedef struct rb_jit_t {
    void *p;
} rb_jit_t;

typedef struct jit_trace jit_trace_t;

typedef struct jit_event_t {
    rb_thread_t *th;
    rb_control_frame_t *cfp;
    VALUE *pc;
    jit_trace_t *trace;
    int opcode;
    int trace_error;
} jit_event_t;

static int get_opcode(rb_control_frame_t *cfp, VALUE *reg_pc)
{
    long pc = (reg_pc - cfp->iseq->iseq_encoded);
    int op = (int) (cfp->iseq->iseq[pc]);
    assert(0 <= op && op < VM_INSTRUCTION_SIZE);
    return op;
}

static jit_event_t *jit_init_event(jit_event_t *e, rb_jit_t *jit, rb_thread_t *th, rb_control_frame_t *cfp, VALUE *pc)
{
    e->th  = th;
    e->cfp = cfp;
    e->pc  = pc;
    e->opcode = get_opcode(e->cfp, e->pc);
    return e;
}

static rb_jit_t *current_jit = NULL;
static VALUE rb_cMath;

static void jit_global_default_params_setup()
{
    char *disable_jit_ptr;
    disable_jit_ptr = getenv("RUJIT_DISABLE_JIT");
    if (disable_jit_ptr != NULL) {
        int disable_jit_i = atoi(disable_jit_ptr);
        if (RTEST(ruby_verbose)) {
            fprintf(stderr, "disable_jit=%d\n", disable_jit_i);
        }
        disable_jit = disable_jit_i;
    }
}

static void jit_default_params_setup(rb_jit_t *jit)
{
}

static rb_jit_t *jit_init()
{
    return NULL;
}

static void jit_delete(rb_jit_t *jit)
{
}

void Init_rawjit()
{
    jit_global_default_params_setup();
    if (!disable_jit) {
        current_jit = jit_init();
        rb_cMath = rb_singleton_class(rb_mMath);
        Init_jit(); // load jit_prelude
    }
}

void Destruct_rawjit()
{
    if (!disable_jit) {
        jit_delete(current_jit);
        current_jit = NULL;
    }
}

static VALUE *trace_selection(rb_jit_t *jit, jit_event_t *e)
{
    return e->pc;
}

VALUE *rb_jit_trace(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc)
{
    if (disable_jit) {
        return reg_pc;
    }
    jit_event_t ebuf;
    jit_event_t *e = jit_init_event(&ebuf, current_jit, th, reg_cfp, reg_pc);
    return trace_selection(current_jit, e);
}
