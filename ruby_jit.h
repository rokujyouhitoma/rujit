/**********************************************************************

  ruby_jit.h -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#include "ruby/ruby.h"
#include "ruby/vm.h"
#include "ruby/st.h"
#include "method.h"
#include "vm_core.h"
#include "vm_exec.h"
#include "vm_insnhelper.h"
#include "internal.h"
#include "iseq.h"

#include "probes.h"
#include "probes_helper.h"
#include "jit.h"

#ifndef JIT_RUBY_JIT_H
#define JIT_RUBY_JIT_H

#define JIT_OP_UNREDEFINED_P(op, klass) (LIKELY((JIT_RUNTIME->redefined_flag[(op)] & (klass)) == 0))
#define JIT_GET_GLOBAL_METHOD_STATE() (*(JIT_RUNTIME)->global_method_state)
#define JIT_GET_GLOBAL_CONSTANT_STATE() (*(JIT_RUNTIME)->global_constant_state)

#ifndef JIT_HOST
typedef VALUE (*jit_native_func0_t)();
typedef VALUE (*jit_native_func1_t)(VALUE);
typedef VALUE (*jit_native_func2_t)(VALUE, VALUE);
typedef VALUE (*jit_native_func3_t)(VALUE, VALUE, VALUE);
typedef VALUE (*jit_native_func4_t)(VALUE, VALUE, VALUE, VALUE);
typedef VALUE (*jit_native_func5_t)(VALUE, VALUE, VALUE, VALUE, VALUE);
typedef VALUE (*jit_native_func6_t)(VALUE, VALUE, VALUE, VALUE, VALUE, VALUE);

void rb_out_of_int(SIGNED_VALUE num) { JIT_RUNTIME->_rb_out_of_int(num); }

VALUE rb_int2big(SIGNED_VALUE v) { return JIT_RUNTIME->_rb_int2big(v); }

VALUE rb_float_new_in_heap(double d)
{
    return JIT_RUNTIME->_rb_float_new_in_heap(d);
}

static inline VALUE make_no_method_exception(VALUE exc, const char *format,
                                             VALUE obj, int argc,
                                             const VALUE *argv)
{
    return JIT_RUNTIME->_make_no_method_exception(exc, format, obj, argc, argv);
}

#define rb_cArray (JIT_RUNTIME->cArray)
#define rb_cFixnum (JIT_RUNTIME->cFixnum)
#define rb_cFloat (JIT_RUNTIME->cFloat)
#define rb_cHash (JIT_RUNTIME->cHash)
#define rb_cRegexp (JIT_RUNTIME->cRegexp)
#define rb_cString (JIT_RUNTIME->cString)
#define rb_cTime (JIT_RUNTIME->cTime)
#define rb_cSymbol (JIT_RUNTIME->cSymbol)

#define rb_cTrueClass (JIT_RUNTIME->cTrueClass)
#define rb_cFalseClass (JIT_RUNTIME->cFalseClass)
#define rb_cNilClass (JIT_RUNTIME->cNilClass)

#define rb_check_array_type (JIT_RUNTIME->_rb_check_array_type)
#define rb_big_plus (JIT_RUNTIME->_rb_big_plus)
#define rb_big_minus (JIT_RUNTIME->_rb_big_minus)
#define rb_big_mul (JIT_RUNTIME->_rb_big_mul)
#define rb_int2big (JIT_RUNTIME->_rb_int2big)
#define rb_str_plus (JIT_RUNTIME->_rb_str_plus)
#define rb_str_append (JIT_RUNTIME->_rb_str_append)
#define rb_str_length (JIT_RUNTIME->_rb_str_length)
#define rb_str_resurrect (JIT_RUNTIME->_rb_str_resurrect)
#define rb_range_new (JIT_RUNTIME->_rb_range_new)
#define rb_hash_new (JIT_RUNTIME->_rb_hash_new)
#define rb_hash_aref (JIT_RUNTIME->_rb_hash_aref)
#define rb_hash_aset (JIT_RUNTIME->_rb_hash_aset)
#define rb_reg_match (JIT_RUNTIME->_rb_reg_match)
#define rb_reg_new_ary (JIT_RUNTIME->_rb_reg_new_ary)
#define rb_ary_new (JIT_RUNTIME->_rb_ary_new)
#define rb_ary_new_from_values (JIT_RUNTIME->_rb_ary_new_from_values)
#define rb_gvar_get (JIT_RUNTIME->_rb_gvar_get)
#define rb_gvar_set (JIT_RUNTIME->_rb_gvar_set)
#define rb_class_new_instance (JIT_RUNTIME->_rb_class_new_instance)
#define rb_obj_as_string (JIT_RUNTIME->_rb_obj_as_string)

#define make_no_method_exception JIT_RUNTIME->_make_no_method_exception
#define rb_ary_entry JIT_RUNTIME->_rb_ary_entry
#define rb_ary_store JIT_RUNTIME->_rb_ary_store
#define ruby_float_mod JIT_RUNTIME->_ruby_float_mod
#define rb_float_new_in_heap JIT_RUNTIME->_rb_float_new_in_heap
#define ruby_vm_redefined_flag JIT_RUNTIME->_ruby_vm_redefined_flag
#define jit_vm_redefined_flag JIT_RUNTIME->_jit_vm_redefined_flag

#define rb_gc_writebarrier JIT_RUNTIME->_rb_gc_writebarrier
#define rb_exc_raise JIT_RUNTIME->_rb_exc_raise
#define rb_out_of_int JIT_RUNTIME->_rb_out_of_int
#define ruby_current_vm JIT_RUNTIME->_ruby_current_vm

#undef CLASS_OF
#define CLASS_OF(O) jit_rb_class_of(O)

static inline VALUE jit_rb_class_of(VALUE obj)
{
    if (IMMEDIATE_P(obj)) {
	if (FIXNUM_P(obj))
	    return rb_cFixnum;
	if (FLONUM_P(obj))
	    return rb_cFloat;
	if (obj == Qtrue)
	    return rb_cTrueClass;
	if (STATIC_SYM_P(obj))
	    return rb_cSymbol;
    }
    else if (!RTEST(obj)) {
	if (obj == Qnil)
	    return rb_cNilClass;
	if (obj == Qfalse)
	    return rb_cFalseClass;
    }
    return RBASIC(obj)->klass;
}

static inline void vm_stackoverflow(void)
{
    rb_exc_raise(sysstack_error);
}

static inline VALUE *
VM_EP_LEP(VALUE *ep)
{
    while (!VM_EP_LEP_P(ep)) {
	ep = VM_EP_PREV_EP(ep);
    }
    return ep;
}

VALUE *
rb_vm_ep_local_ep(VALUE *ep)
{
    return VM_EP_LEP(ep);
}

static inline VALUE *
VM_CF_LEP(rb_control_frame_t *cfp)
{
    return VM_EP_LEP(cfp->ep);
}

static inline VALUE *
VM_CF_PREV_EP(rb_control_frame_t *cfp)
{
    return VM_EP_PREV_EP((cfp)->ep);
}

static inline rb_block_t *
VM_CF_BLOCK_PTR(rb_control_frame_t *cfp)
{
    VALUE *ep = VM_CF_LEP(cfp);
    return VM_EP_BLOCK_PTR(ep);
}

static inline rb_control_frame_t *
jit_vm_push_frame(rb_thread_t *th,
                  const rb_iseq_t *iseq,
                  VALUE type,
                  VALUE self,
                  VALUE klass,
                  VALUE specval,
                  const VALUE *pc,
                  VALUE *sp,
                  int local_size,
                  const rb_method_entry_t *me,
                  int stack_max)
{
    rb_control_frame_t *const cfp = th->cfp - 1;
    int i;

    /* check stack overflow */
    CHECK_VM_STACK_OVERFLOW0(cfp, sp, local_size + stack_max);

    th->cfp = cfp;

    /* setup vm value stack */

    /* initialize local variables */
    for (i = 0; i < local_size; i++) {
	*sp++ = Qnil;
    }

    /* set special val */
    *sp = specval;

    /* setup vm control frame stack */

    cfp->pc = (VALUE *)pc;
    cfp->sp = sp + 1;
#if VM_DEBUG_BP_CHECK
    cfp->bp_check = sp + 1;
#endif
    cfp->ep = sp;
    cfp->iseq = (rb_iseq_t *)iseq;
    cfp->flag = type;
    cfp->self = self;
    cfp->block_iseq = 0;
    cfp->proc = 0;
    cfp->me = me;
    if (klass) {
	cfp->klass = klass;
    }
    else {
	rb_control_frame_t *prev_cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
	if (RUBY_VM_CONTROL_FRAME_STACK_OVERFLOW_P(th, prev_cfp)) {
	    cfp->klass = Qnil;
	}
	else {
	    cfp->klass = prev_cfp->klass;
	}
    }

    if (VMDEBUG == 2) {
	SDR();
    }

    return cfp;
}

static inline void
vm_pop_frame(rb_thread_t *th)
{
    th->cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(th->cfp);

    if (VMDEBUG == 2) {
	SDR();
    }
}

static inline int jit_block_proc_is_lambda(const VALUE procval)
{
    rb_proc_t *proc;

    if (procval) {
	GetProcPtr(procval, proc);
	return proc->is_lambda;
    }
    else {
	return 0;
    }
}

static int jit_vm_yield_setup_block_args(rb_thread_t *th, const rb_iseq_t *iseq,
                                         int orig_argc, VALUE *argv,
                                         const rb_block_t *blockptr)
{
    int i;
    int argc = orig_argc;
    const int m = iseq->argc;
    const int min = m + iseq->arg_post_len;
    VALUE ary, arg0;
    VALUE keyword_hash = Qnil;
    int opt_pc = 0;

    th->mark_stack_len = argc;

    /*
     * yield [1, 2]
     *  => {|a|} => a = [1, 2]
     *  => {|a, b|} => a, b = [1, 2]
     */
    arg0 = argv[0];
    if (!(iseq->arg_simple & 0x02) && /* exclude {|a|} */
        (min > 0 || /* positional arguments exist */
         iseq->arg_opts > 2 || /* multiple optional arguments exist */
         iseq->arg_keyword != -1 || /* any keyword arguments */
         0) && argc == 1
        && !NIL_P(ary = rb_check_array_type(arg0))) { /* rhs is only an array */
	th->mark_stack_len = argc = RARRAY_LENINT(ary);

	CHECK_VM_STACK_OVERFLOW(th->cfp, argc);

	MEMCPY(argv, RARRAY_CONST_PTR(ary), VALUE, argc);
    }
    else {
	/* vm_push_frame current argv is at the top of sp because
	 * vm_invoke_block
	 * set sp at the first element of argv.
	 * Therefore when rb_check_array_type(arg0) called to_ary and called
	 * to_ary
	 * or method_missing run vm_push_frame, it initializes local variables.
	 * see also https://bugs.ruby-lang.org/issues/8484
	 */
	argv[0] = arg0;
    }

    /* keyword argument */
    assert(iseq->arg_keyword == -1 && "keyword argument is not implemented");

    for (i = argc; i < m; i++) {
	argv[i] = Qnil;
    }

    if (iseq->arg_rest == -1 && iseq->arg_opts == 0) {
	const int arg_size = iseq->arg_size;
	if (arg_size < argc) {
	    /*
	     * yield 1, 2
	     * => {|a|} # truncate
	     */
	    th->mark_stack_len = argc = arg_size;
	}
    }
    else {
	int r = iseq->arg_rest;

	if (iseq->arg_post_len
	    || iseq->arg_opts) { /* TODO: implement simple version for
				    (iseq->arg_post_len==0 && iseq->arg_opts >
				    0) */
	    assert(0 && "FIXME implement vm_yield_setup_block_args_complex");
	    // opt_pc = vm_yield_setup_block_args_complex(th, iseq, argc, argv);
	}
	else {
	    if (argc < r) {
		/* yield 1
		 * => {|a, b, *r|}
		 */
		for (i = argc; i < r; i++) {
		    argv[i] = Qnil;
		}
		argv[r] = rb_ary_new();
	    }
	    else {
		argv[r] = rb_ary_new4(argc - r, &argv[r]);
	    }
	}

	th->mark_stack_len = iseq->arg_size;
    }

    /* keyword argument */
    if (iseq->arg_keyword != -1) {
	int arg_keywords_end = iseq->arg_keyword - (iseq->arg_block != -1);
	for (i = iseq->arg_keywords; 0 < i; i--) {
	    argv[arg_keywords_end - i] = Qnil;
	}
	argv[iseq->arg_keyword] = keyword_hash;
    }

    /* {|&b|} */
    assert(iseq->arg_block == -1 && "{|&b|} is not implemented");
    // if (iseq->arg_block != -1) {
    //  VALUE procval = Qnil;
    //
    //  if (blockptr) {
    //    if (blockptr->proc == 0) {
    //      procval = rb_vm_make_proc(th, blockptr, rb_cProc);
    //    }
    //    else {
    //      procval = blockptr->proc;
    //    }
    //  }
    //
    //  argv[iseq->arg_block] = procval;
    //}

    th->mark_stack_len = 0;
    return opt_pc;
}
static inline void vm_callee_setup_arg(rb_thread_t *th, rb_call_info_t *ci,
                                       const rb_iseq_t *iseq, VALUE *argv,
                                       int is_lambda)
{
    if (LIKELY(iseq->arg_simple & 0x01)) {
	/* simple check */
	assert(ci->argc == iseq->argc);
	ci->aux.opt_pc = 0;
    }
    else {
	assert(0 && "FIXME support complex case");
	// ci->aux.opt_pc = vm_callee_setup_arg_complex(th, ci, iseq, argv,
	// is_lambda > 1);
    }
}

static inline int
jit_vm_yield_setup_args(rb_thread_t *const th, const rb_iseq_t *iseq, int argc,
                        VALUE *argv, const rb_block_t *blockptr, int lambda)
{
    if (0) { /* for debug */
	printf("     argc: %d\n", argc);
	printf("iseq argc: %d\n", iseq->argc);
	printf("iseq opts: %d\n", iseq->arg_opts);
	printf("iseq rest: %d\n", iseq->arg_rest);
	printf("iseq post: %d\n", iseq->arg_post_len);
	printf("iseq blck: %d\n", iseq->arg_block);
	printf("iseq smpl: %d\n", iseq->arg_simple);
	printf("   lambda: %s\n", lambda ? "true" : "false");
    }

    if (lambda) {
	/* call as method */
	rb_call_info_t ci_entry;
	ci_entry.flag = 0;
	ci_entry.argc = argc;
	ci_entry.blockptr = (rb_block_t *)blockptr;
	vm_callee_setup_arg(th, &ci_entry, iseq, argv, lambda);
	return ci_entry.aux.opt_pc;
    }
    else {
	return jit_vm_yield_setup_block_args(th, iseq, argc, argv, blockptr);
    }
}

/* copied from vm_invoke_block() */
static inline VALUE jit_vm_call_block_setup(rb_thread_t *th,
                                            rb_control_frame_t *reg_cfp,
                                            rb_block_t *block,
                                            rb_call_info_t *ci, int argc)
{
    rb_iseq_t *iseq = block->iseq;

    assert(UNLIKELY(ci->flag & VM_CALL_ARGS_SPLAT) == 0);
    assert(BUILTIN_TYPE(iseq) != T_NODE);
    int opt_pc;
    const int arg_size = iseq->arg_size;
    int is_lambda = jit_block_proc_is_lambda(block->proc);
    VALUE *const rsp = GET_SP() - argc;
    SET_SP(rsp);

    opt_pc = jit_vm_yield_setup_args(th, iseq, argc, rsp, 0, is_lambda * 2);

    jit_vm_push_frame(th, iseq,
                      is_lambda ? VM_FRAME_MAGIC_LAMBDA : VM_FRAME_MAGIC_BLOCK,
                      block->self, block->klass, VM_ENVVAL_PREV_EP_PTR(block->ep),
                      iseq->iseq_encoded + opt_pc, rsp + arg_size,
                      iseq->local_size - arg_size, 0, iseq->stack_max);

    return Qundef;
}

static inline void jit_vm_pop_frame(rb_thread_t *th)
{
    th->cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(th->cfp);
}
static inline VALUE jit_vm_call_iseq_setup_normal(rb_thread_t *th, rb_control_frame_t *cfp, CALL_INFO ci, int argc)
{
    int i, local_size;
    VALUE *argv = cfp->sp - ci->argc;
    rb_iseq_t *iseq = ci->me->def->body.iseq;
    VALUE *sp = argv + iseq->arg_size;

    /* clear local variables (arg_size...local_size) */
    for (i = iseq->arg_size, local_size = iseq->local_size; i < local_size;
         i++) {
	*sp++ = Qnil;
    }

    jit_vm_push_frame(th, iseq, VM_FRAME_MAGIC_METHOD, ci->recv, ci->defined_class,
                      VM_ENVVAL_BLOCK_PTR(ci->blockptr),
                      iseq->iseq_encoded + ci->aux.opt_pc, sp, 0, ci->me,
                      iseq->stack_max);

    cfp->sp = argv - 1 /* recv */;
    return Qundef;
}

#endif
#include "lir_template.h"
#define __int3__ asm volatile("int3");
#endif /* end of include guard */
