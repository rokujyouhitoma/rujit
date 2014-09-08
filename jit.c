/**********************************************************************

  jit.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/vm.h"
#include "vm_core.h"
#include "internal.h"
#include "jit_opts.h"
#include "jit_prelude.c"
#include "insns.inc"
#include "insns_info.inc"

static int disable_jit = 0;

typedef struct rb_jit_t rb_jit_t;

typedef struct jit_trace trace_t;

typedef struct jit_event_t {
    rb_thread_t *th;
    rb_control_frame_t *cfp;
    VALUE *pc;
    trace_t *trace;
    int opcode;
    int trace_error;
} jit_event_t;

static int get_opcode(rb_control_frame_t *cfp, VALUE *reg_pc)
{
    long pc = (reg_pc - cfp->iseq->iseq_encoded);
    int op = (int)(cfp->iseq->iseq[pc]);
    assert(0 <= op && op < VM_INSTRUCTION_SIZE);
    return op;
}

static int get_opcode(rb_control_frame_t *cfp, VALUE *pc);

static void dump_inst(jit_event_t *e)
{
#if DUMP_INST > 0
    long pc = (e->pc - e->cfp->iseq->iseq_encoded);
    fprintf(stderr, "%04ld pc=%p %02d %s\n",
            pc, e->pc, e->opcode, insn_name(e->opcode));
#endif
}

static jit_event_t *jit_init_event(jit_event_t *e, rb_jit_t *jit, rb_thread_t *th, rb_control_frame_t *cfp, VALUE *pc)
{
    e->th = th;
    e->cfp = cfp;
    e->pc = pc;
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
    rb_jit_t *jit = NULL;
    jit_default_params_setup(jit);
    return jit;
}

static void jit_delete(rb_jit_t *jit)
{
    if (jit) {
	//
    }
    // remove compiler warnings
    (void)insn_op_types;
    (void)insn_op_type;
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

#define HOT_TRACE_THRESHOLD 4
struct jit_trace {
    VALUE *last_pc;
    long counter;
};

typedef struct trace_recorder_t {
} trace_recorder_t;

struct rb_jit_t {
    trace_recorder_t *recorder;
};

static int is_recording(rb_jit_t *jit)
{
    return 0;
}

static void set_recording(rb_jit_t *jit)
{
}

static trace_t *create_new_trace(rb_jit_t *jit, jit_event_t *e)
{
    return NULL;
}

static trace_t *find_trace(rb_jit_t *jit, jit_event_t *e)
{
    return NULL;
}

static int is_backward_branch(jit_event_t *e, VALUE **target_pc_ptr)
{
    return 0;
}

static int is_compiled_trace(trace_t *trace)
{
    return 0;
}

static int is_end_of_trace(rb_jit_t *jit, jit_event_t *e)
{
    return 0;
}

static void append_to_tracebuffer(trace_recorder_t *rec, jit_event_t *e)
{
}

static void compile_trace(rb_jit_t *jit)
{
}

static VALUE *trace_invoke(rb_jit_t *jit, jit_event_t *e, trace_t *trace)
{
    return e->pc;
}

static void trace_reset(trace_t *trace, int alloc_memory)
{
}

static void tracebuffer_clear(trace_recorder_t *rec, int alloc_memory)
{
}

static void tracebuffer_set_trace(trace_recorder_t *rec, trace_t *trace)
{
}

static VALUE *trace_selection(rb_jit_t *jit, jit_event_t *e)
{
    trace_t *trace = NULL;
    VALUE *target_pc = NULL;
    if (is_recording(jit)) {
	if (is_end_of_trace(jit, e)) {
	    compile_trace(jit);
	}
	else {
	    append_to_tracebuffer(jit->recorder, e);
	}
	return e->pc;
    }
    trace = find_trace(jit, e);
    if (is_compiled_trace(trace)) {
	return trace_invoke(jit, e, trace);
    }
    if (is_backward_branch(e, &target_pc)) {
	if (!trace) {
	    trace = create_new_trace(jit, e);
	}
	trace->last_pc = target_pc;
    }
    if (trace) {
	trace->counter += 1;
	if (trace->counter > HOT_TRACE_THRESHOLD) {
	    set_recording(jit);
	    tracebuffer_set_trace(jit->recorder, trace);
	    trace_reset(trace, 1);
	    tracebuffer_clear(jit->recorder, 1);
	    append_to_tracebuffer(jit->recorder, e);
	}
    }
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
