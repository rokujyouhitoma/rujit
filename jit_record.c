/**********************************************************************

  record.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/
//#define CREATE_BLOCK(REC, PC) basicblock_new(&(REC)->mpool, PC)
//
//#undef GET_GLOBAL_CONSTANT_STATE
//#define GET_GLOBAL_CONSTANT_STATE() (*ruby_vm_global_constant_state_ptr)
//
//#define not_support_op(rec, CFP, PC, OPNAME)                                \
//    do {                                                                    \
//        fprintf(stderr, "exit trace : not support bytecode: " OPNAME "\n"); \
//        TracerecorderAbort(rec, CFP, PC, TRACE_ERROR_UNSUPPORT_OP);         \
//        return;                                                             \
//    } while (0)
//
//#define EmitIR(OP, ...) Emit_##OP(rec, ##__VA_ARGS__)
//
//#define _POP() regstack_pop(&(rec)->regstack)
//#define _PUSH(REG) regstack_push(&(rec)->regstack, REG)
//#define _TOPN(N) regstack_top(&(rec)->regstack, (int)(N))
//#define _SET(N, REG) regstack_set(&(rec)->regstack, (int)(N), REG)
//
//static lir_t EmitLoadConst(trace_recorder_t *rec, VALUE val)
//{
//    lir_t Rval = NULL;
//    basicblock_t *BB = rec->entry_bb;
//    basicblock_t *prevBB = rec->cur_bb;
//    unsigned inst_size = basicblock_size(BB);
//    rec->cur_bb = BB;
//    if (NIL_P(val)) {
//        Rval = EmitIR(LoadConstNil);
//    } else if (val == Qtrue || val == Qfalse) {
//        Rval = EmitIR(LoadConstBoolean, val);
//    } else if (FIXNUM_P(val)) {
//        Rval = EmitIR(LoadConstFixnum, val);
//    } else if (FLONUM_P(val)) {
//        Rval = EmitIR(LoadConstFloat, val);
//    } else if (!SPECIAL_CONST_P(val)) {
//        if (RBASIC_CLASS(val) == rb_cString) {
//            Rval = EmitIR(LoadConstString, val);
//        } else if (RBASIC_CLASS(val) == rb_cRegexp) {
//            Rval = EmitIR(LoadConstRegexp, val);
//        }
//    }
//
//    if(Rval == NULL) {
//        Rval = EmitIR(LoadConstObject, val);
//    }
//    if (inst_size != basicblock_size(BB)) {
//        TracerecorderAddConst(rec, Rval, val);
//        basicblock_swap_inst(rec->entry_bb, inst_size-1, inst_size);
//    }
//    rec->cur_bb = prevBB;
//    return Rval;
//}
//
//static lir_t EmitSpecialInst_FixnumPowOverflow(trace_recorder_t *rec, CALL_INFO ci,
//                                        lir_t *regs)
//{
//    return Emit_InvokeNative(rec, ci->me->def->body.cfunc.func, 2, regs);
//}
//
//static lir_t EmitSpecialInst_FloatPow(trace_recorder_t *rec, CALL_INFO ci,
//                                        lir_t *regs)
//{
//    return Emit_InvokeNative(rec, ci->me->def->body.cfunc.func, 2, regs);
//}
//
//static lir_t EmitSpecialInst_FixnumSucc(trace_recorder_t *rec, CALL_INFO ci,
//                                        lir_t *regs)
//{
//    lir_t Robj = EmitIR(LoadConstFixnum, INT2FIX(1));
//    return EmitIR(FixnumAddOverflow, regs[0], Robj);
//}
//
//static lir_t EmitSpecialInst_TimeSucc(trace_recorder_t *rec, CALL_INFO ci,
//                                      lir_t *regs)
//{
//    return Emit_InvokeNative(rec, rb_time_succ, 1, regs);
//}
//
//static lir_t EmitSpecialInst_StringFreeze(trace_recorder_t *rec, CALL_INFO ci,
//                                          lir_t *regs)
//{
//    return _POP();
//}
//
////static lir_t EmitSpecialInst_FixnumToFixnum(trace_recorder_t * rec, CALL_INFO ci,
////                                            lir_t* regs)
////{
////    return regs[0];
////}
//
//static lir_t EmitSpecialInst_FloatToFloat(trace_recorder_t * rec, CALL_INFO ci,
//                                          lir_t* regs)
//{
//    return regs[0];
//}
//
//static lir_t EmitSpecialInst_StringToString(trace_recorder_t * rec, CALL_INFO ci,
//                                            lir_t* regs)
//{
//    return regs[0];
//}
//
//extern VALUE rb_obj_not(VALUE obj);
//
//static lir_t EmitSpecialInst_ObjectNot(trace_recorder_t * rec, CALL_INFO ci,
//                                       lir_t* regs)
//{
//    ci = trace_recorder_clone_cache(rec, ci);
//    EmitIR(GuardMethodCache, rec->current_event->pc, regs[0], ci);
//    if (check_cfunc(ci->me, rb_obj_not)) {
//        return EmitIR(ObjectNot, regs[0]);
//    } else {
//        return EmitIR(InvokeMethod, ci, ci->argc + 1, regs);
//    }
//}
//
////static lir_t EmitSpecialInst_ObjectNe(trace_recorder_t *rec, CALL_INFO ci,
////                                        lir_t *regs)
////{
////    ci = trace_recorder_clone_cache(rec, ci);
////    EmitIR(GuardMethodCache, rec->current_event->pc, regs[0], ci);
////    return EmitIR(InvokeMethod, ci, ci->argc + 1, regs);
////}
////
////static lir_t EmitSpecialInst_ObjectEq(trace_recorder_t *rec, CALL_INFO ci,
////                                        lir_t *regs)
////{
////    ci = trace_recorder_clone_cache(rec, ci);
////    EmitIR(GuardMethodCache, rec->current_event->pc, regs[0], ci);
////    return EmitIR(InvokeMethod, ci, ci->argc + 1, regs);
////}
//
//static lir_t EmitSpecialInst_GetPropertyName(trace_recorder_t *rec, CALL_INFO ci,
//                                             lir_t *regs)
//{
//    VALUE obj = ci->recv;
//    VALUE *reg_pc = rec->current_event->pc;
//    int cacheable = vm_load_cache(obj, ci->me->def->body.attr.id, 0, ci, 1);
//    assert(ci->aux.index > 0 && cacheable);
//    EmitIR(GuardProperty, reg_pc, regs[0], 1 /*is_attr*/, (void *)ci);
//    return Emit_GetPropertyName(rec, regs[0], ci->aux.index - 1);
//}
//
//static lir_t EmitSpecialInst_SetPropertyName(trace_recorder_t *rec, CALL_INFO ci,
//                                             lir_t *regs)
//{
//    VALUE obj = ci->recv;
//    VALUE *reg_pc = rec->current_event->pc;
//    int cacheable = vm_load_cache(obj, ci->me->def->body.attr.id, 0, ci, 1);
//    assert(ci->aux.index > 0 && cacheable);
//    EmitIR(GuardProperty, reg_pc, regs[0], 1 /*is_attr*/, (void *)ci);
//    return Emit_SetPropertyName(rec, regs[0], ci->aux.index - 1, regs[1]);
//}
//
static lir_t EmitSpecialInst(trace_recorder_t *rec, VALUE *pc, CALL_INFO ci,
                             enum ruby_vminsn_type opcode, VALUE *params,
                             lir_t *regs);
#include "yarv2lir.c"
//
//static void EmitSpecialInst0(trace_recorder_t *rec, VALUE *pc, CALL_INFO ci,
//                             int opcode, VALUE *params, lir_t *regs)
//{
//    lir_t Rval = NULL;
//    vm_search_method(ci, params[0]);
//    if ((Rval = EmitSpecialInst(rec, pc, ci, opcode, params, regs)) == NULL) {
//        ci = trace_recorder_clone_cache(rec, ci);
//        EmitIR(GuardMethodCache, pc, regs[0], ci);
//        Rval = EmitIR(InvokeMethod, ci, ci->argc + 1, regs);
//    }
//    _PUSH(Rval);
//}
//
//static void PrepareInstructionArgument(trace_recorder_t *rec,
//                                       rb_control_frame_t *reg_cfp, int argc,
//                                       VALUE *params, lir_t *regs)
//{
//    int i;
//    for (i = 0; i < argc + 1; i++) {
//        params[i] = TOPN(argc - i);
//        regs[argc - i] = _POP();
//    }
//}
//
//static void EmitSpecialInst1(trace_recorder_t *rec, rb_control_frame_t *reg_cfp,
//                             VALUE *reg_pc, int opcode)
//{
//    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
//    lir_t regs[ci->argc + 1];
//    VALUE params[ci->argc + 1];
//    TakeStackSnapshot(rec, reg_pc);
//    PrepareInstructionArgument(rec, reg_cfp, ci->argc, params, regs);
//    EmitSpecialInst0(rec, reg_pc, ci, opcode, params, regs);
//}
//
//static void EmitPushFrame(trace_recorder_t *rec, rb_control_frame_t *reg_cfp,
//        VALUE *reg_pc, CALL_INFO ci,
//        lir_t Rblock, rb_block_t *block)
//{
//    int i, argc = ci->orig_argc + 1 /*recv*/;
//    lir_t regs[argc];
//    VALUE params[argc];
//
//    for (i = 0; i < argc; i++) {
//        params[i] = TOPN(ci->orig_argc - i);
//        regs[ci->orig_argc - i] = _POP();
//    }
//
//    rec->CallDepth += 1;
//    EmitIR(GuardMethodCache, reg_pc, regs[0], ci);
//    if (Rblock) {
//        EmitIR(GuardBlockEqual, reg_pc, Rblock, (VALUE)block);
//    }
//    PushCallStack(rec, argc, regs, 1);
//    _PUSH(EmitIR(FramePush, reg_pc, ci, 0, Rblock, argc, regs));
//}
//
//static void EmitNewInstance(trace_recorder_t *rec, rb_control_frame_t *reg_cfp,
//                            VALUE *reg_pc, CALL_INFO ci,
//                            VALUE *params, lir_t *regs)
//{
//    int argc = ci->argc;
//    if ((ci->flag & VM_CALL_ARGS_BLOCKARG) || ci->blockiseq != 0) {
//        fprintf(stderr, "Class.new with block is not supported\n");
//        TracerecorderAbort(rec, reg_cfp, reg_pc, TRACE_ERROR_UNSUPPORT_OP);
//        return;
//    }
//    _PUSH(EmitIR(AllocObject, regs[0], argc, regs + 1));
//}
//
//static void EmitJump(trace_recorder_t *rec, VALUE *pc, int link)
//{
//    basicblock_t *bb = NULL;
//    if (link == 0) {
//        bb = CREATE_BLOCK(rec, pc);
//    } else {
//        if ((bb = FindBasicBlockByPC(rec, pc)) == NULL) {
//            bb = CREATE_BLOCK(rec, pc);
//        }
//    }
//    EmitIR(Jump, bb);
//    rec->cur_bb = bb;
//}
//
//static void EmitMethodCall(trace_recorder_t *rec, rb_control_frame_t *reg_cfp,
//                           VALUE *reg_pc, CALL_INFO ci, rb_block_t *block,
//                           lir_t Rblock, int opcode)
//{
//    int i;
//    lir_t Rval = NULL;
//    lir_t regs[ci->argc + 1];
//    VALUE params[ci->argc + 1];
//
//    vm_search_method(ci, ci->recv = TOPN(ci->argc));
//    ci = trace_recorder_clone_cache(rec, ci);
//
//    // user defined ruby method
//    if (ci->me && ci->me->def->type == VM_METHOD_TYPE_ISEQ) {
//        // FIXME we need to implement vm_callee_setup_arg()
//        EmitPushFrame(rec, reg_cfp, reg_pc, ci, Rblock, block);
//        //EmitJump(rec, reg_pc, 0);
//        return;
//    }
//
//    PrepareInstructionArgument(rec, reg_cfp, ci->argc, params, regs);
//    if ((Rval = EmitSpecialInst(rec, reg_pc, ci, opcode, params, regs)) != NULL) {
//        _PUSH(Rval);
//        return;
//    }
//
//    // check ClassA.new(argc, argv)
//    if (check_cfunc(ci->me, rb_class_new_instance)) {
//        if (ci->me->klass == rb_cClass) {
//            return EmitNewInstance(rec, reg_cfp, reg_pc, ci, params, regs);
//        }
//    }
//
//    // check block_given?
//    {
//        extern VALUE rb_f_block_given_p(void);
//        if (check_cfunc(ci->me, rb_f_block_given_p)) {
//            lir_t Rrecv = EmitIR(LoadSelf);
//            EmitIR(GuardMethodCache, reg_pc, Rrecv, ci);
//            _PUSH(EmitIR(InvokeNative, rb_f_block_given_p, 0, NULL));
//            return;
//        }
//    }
//
//    RJitSetMode(rec->jit, rec->jit->mode_ | TRACE_MODE_EMIT_BACKWARD_BRANCH);
//#if 0
//    ci = trace_recorder_clone_cache(rec, ci);
//    EmitIR(GuardMethodCache, reg_pc, regs[0], ci);
//    Rval = EmitIR(InvokeMethod, ci, ci->argc + 1, regs);
//    _PUSH(Rval);
//    reg_pc = reg_pc + insn_len(opcode);
//#else
//    // re-push registers
//    for (i = 0; i < ci->argc + 1; i++) {
//        _PUSH(regs[i]);
//    }
//#endif
//    TakeStackSnapshot(rec, reg_pc);
//    TracerecorderAbort(rec, reg_cfp, reg_pc, TRACE_ERROR_NATIVE_METHOD);
//    return;
//}
//
//static void _record_getlocal(trace_recorder_t *rec, rb_num_t level, lindex_t idx)
//{
//    lir_t Rval = EmitIR(EnvLoad, (int)level, (int)idx);
//    _PUSH(Rval);
//}
//
//static void _record_setlocal(trace_recorder_t *rec, rb_num_t level, lindex_t idx)
//{
//    lir_t Rval = _POP();
//    EmitIR(EnvStore, (int)level, (int)idx, Rval);
//}
//
//// record API
//
static void record_nop(trace_recorder_t *rec, jit_event_t *e)
{
    /* do nothing */
}

static void record_getlocal(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t level = (rb_num_t)GET_OPERAND(2);
    //    lindex_t idx = (lindex_t)GET_OPERAND(1);
    //    _record_getlocal(rec, level, idx);
}

static void record_setlocal(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t level = (rb_num_t)GET_OPERAND(2);
    //    lindex_t idx = (lindex_t)GET_OPERAND(1);
    //    _record_setlocal(rec, level, idx);
}

static void record_getspecial(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "getspecial");
}

static void record_setspecial(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "setspecial");
}

static void record_getinstancevariable(trace_recorder_t *rec, jit_event_t *e)
{
    //    IC ic = (IC)GET_OPERAND(2);
    //    ID id = (ID)GET_OPERAND(1);
    //    VALUE obj = GET_SELF();
    //    lir_t Rrecv = EmitIR(LoadSelf);
    //
    //    if (vm_load_cache(obj, id, ic, NULL, 0)) {
    //        TakeStackSnapshot(rec, reg_pc);
    //        EmitIR(GuardTypeObject, reg_pc, Rrecv);
    //        EmitIR(GuardProperty, reg_pc, Rrecv, 0 /*!is_attr*/, (void *)ic);
    //        _PUSH(EmitIR(GetPropertyName, Rrecv, ic->ic_value.index));
    //        return;
    //    }
    //    not_support_op(rec, reg_cfp, reg_pc, "getinstancevariable");
}

static void record_setinstancevariable(trace_recorder_t *rec, jit_event_t *e)
{
    //    IC ic = (IC)GET_OPERAND(2);
    //    ID id = (ID)GET_OPERAND(1);
    //    VALUE obj = GET_SELF();
    //    lir_t Rrecv = EmitIR(LoadSelf);
    //
    //    int cacheable = vm_load_cache(obj, id, ic, NULL, 0);
    //    if (cacheable) {
    //        TakeStackSnapshot(rec, reg_pc);
    //        EmitIR(GuardTypeObject, reg_pc, Rrecv);
    //        EmitIR(GuardProperty, reg_pc, Rrecv, 0 /*!is_attr*/, (void *)ic);
    //        EmitIR(SetPropertyName, Rrecv, ic->ic_value.index, _POP());
    //        return;
    //    }
    //
    //    not_support_op(rec, reg_cfp, reg_pc, "setinstancevariable");
}

static void record_getclassvariable(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "getclassvariable");
}

static void record_setclassvariable(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "setclassvariable");
}

static void record_getconstant(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "getconstant");
}

static void record_setconstant(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "setconstant");
}

static void record_getglobal(trace_recorder_t *rec, jit_event_t *e)
{
    //    GENTRY entry = (GENTRY)GET_OPERAND(1);
    //    lir_t Id = EmitLoadConst(rec, entry);
    //    _PUSH(EmitIR(GetGlobal, Id));
}

static void record_setglobal(trace_recorder_t *rec, jit_event_t *e)
{
    //    GENTRY entry = (GENTRY)GET_OPERAND(1);
    //    lir_t Rval = _POP();
    //    lir_t Id = EmitLoadConst(rec, entry);
    //    EmitIR(SetGlobal, Id, Rval);
}

static void record_putnil(trace_recorder_t *rec, jit_event_t *e)
{
    //    _PUSH(EmitIR(LoadConstNil));
}

static void record_putself(trace_recorder_t *rec, jit_event_t *e)
{
    //    _PUSH(EmitIR(LoadSelf));
}

static void record_putobject(trace_recorder_t *rec, jit_event_t *e)
{
    //    VALUE val = (VALUE)GET_OPERAND(1);
    //    _PUSH(EmitLoadConst(rec, val));
}

/* copied from vm_insnhelper.c */
extern NODE *rb_vm_get_cref(const rb_iseq_t *iseq, const VALUE *ep);
static inline VALUE jit_vm_get_cbase(const rb_iseq_t *iseq, const VALUE *ep)
{
    NODE *cref = rb_vm_get_cref(iseq, ep);
    VALUE klass = Qundef;

    while (cref) {
	if ((klass = cref->nd_clss) != 0) {
	    break;
	}
	cref = cref->nd_next;
    }

    return klass;
}

static inline VALUE jit_vm_get_const_base(const rb_iseq_t *iseq, const VALUE *ep)
{
    NODE *cref = rb_vm_get_cref(iseq, ep);
    VALUE klass = Qundef;

    while (cref) {
	if (!(cref->flags & NODE_FL_CREF_PUSHED_BY_EVAL)
	    && (klass = cref->nd_clss) != 0) {
	    break;
	}
	cref = cref->nd_next;
    }

    return klass;
}

static void record_putspecialobject(trace_recorder_t *rec, jit_event_t *e)
{
    //    enum vm_special_object_type type
    //        = (enum vm_special_object_type)GET_OPERAND(1);
    //    VALUE val = 0;
    //    switch (type) {
    //    case VM_SPECIAL_OBJECT_VMCORE:
    //        val = rb_mRubyVMFrozenCore;
    //        break;
    //    case VM_SPECIAL_OBJECT_CBASE:
    //        val = jit_vm_get_cbase(GET_ISEQ(), GET_EP());
    //        break;
    //    case VM_SPECIAL_OBJECT_CONST_BASE:
    //        val = jit_vm_get_const_base(GET_ISEQ(), GET_EP());
    //        break;
    //    default:
    //        rb_bug("putspecialobject insn: unknown value_type");
    //    }
    //    _PUSH(EmitLoadConst(rec, val));
}

static void record_putiseq(trace_recorder_t *rec, jit_event_t *e)
{
    //    ISEQ iseq = (ISEQ)GET_OPERAND(1);
    //    _PUSH(EmitLoadConst(rec, iseq->self));
}

static void record_putstring(trace_recorder_t *rec, jit_event_t *e)
{
    //    VALUE val = (VALUE)GET_OPERAND(1);
    //    lir_t Rstr = EmitLoadConst(rec, val);
    //    _PUSH(EmitIR(AllocString, Rstr));
}

static void record_concatstrings(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t num = (rb_num_t)GET_OPERAND(1);
    //    rb_num_t i = num - 1;
    //
    //    lir_t Rval = EmitIR(AllocString, _TOPN(i));
    //    while (i-- > 0) {
    //        Rval = EmitIR(StringAdd, Rval, _TOPN(i));
    //    }
    //    for (i = 0; i < num; i++) {
    //        _POP();
    //    }
    //    _PUSH(Rval);
}

static void record_tostring(trace_recorder_t *rec, jit_event_t *e)
{
    //    lir_t Rval = EmitIR(ObjectToString, _POP());
    //    _PUSH(Rval);
}

static void record_toregexp(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t i;
    //    rb_num_t cnt = (rb_num_t)GET_OPERAND(2);
    //    rb_num_t opt = (rb_num_t)GET_OPERAND(1);
    //    lir_t regs[cnt];
    //    lir_t Rary;
    //    for (i = 0; i < cnt; i++) {
    //        regs[cnt - i - 1] = _POP();
    //    }
    //    Rary = EmitIR(AllocArray, (int)cnt, regs);
    //    _PUSH(EmitIR(AllocRegexFromArray, Rary, (int)opt));
}

static void record_newarray(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t i, num = (rb_num_t)GET_OPERAND(1);
    //    lir_t argv[num];
    //    for (i = 0; i < num; i++) {
    //        argv[i] = _POP();
    //    }
    //    _PUSH(EmitIR(AllocArray, (int)num, argv));
}

static void record_duparray(trace_recorder_t *rec, jit_event_t *e)
{
    //    VALUE val = (VALUE)GET_OPERAND(1);
    //    lir_t Rval = EmitLoadConst(rec, val);
    //    lir_t argv[] = { Rval };
    //    _PUSH(EmitIR(InvokeNative, rb_ary_resurrect, 1, argv));
}

static void record_expandarray(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "expandarray");
}

static void record_concatarray(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "concatarray");
}

static void record_splatarray(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "splatarray");
}

static void record_newhash(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t i, num = (rb_num_t)GET_OPERAND(1);
    //    lir_t argv[num];
    //    for (i = num; i > 0; i -= 2) {
    //        argv[i - 1] = _POP(); // key
    //        argv[i - 2] = _POP(); // val
    //    }
    //    _PUSH(EmitIR(AllocHash, (int)num, argv));
}

static void record_newrange(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t flag = (rb_num_t)GET_OPERAND(1);
    //    lir_t Rhigh = _POP();
    //    lir_t Rlow = _POP();
    //    _PUSH(EmitIR(AllocRange, Rlow, Rhigh, (int)flag));
}

static void record_pop(trace_recorder_t *rec, jit_event_t *e)
{
    //    _POP();
}

static void record_dup(trace_recorder_t *rec, jit_event_t *e)
{
    //    lir_t Rval = _POP();
    //    _PUSH(Rval);
    //    _PUSH(Rval);
}

static void record_dupn(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t i, n = (rb_num_t)GET_OPERAND(1);
    //    lir_t argv[n];
    //    // FIXME optimize
    //    for (i = 0; i < n; i++) {
    //        argv[i] = _TOPN(n - i - 1);
    //    }
    //    for (i = 0; i < n; i++) {
    //        _PUSH(argv[i]);
    //    }
}

static void record_swap(trace_recorder_t *rec, jit_event_t *e)
{
    //    lir_t Rval = _POP();
    //    lir_t Robj = _POP();
    //    _PUSH(Robj);
    //    _PUSH(Rval);
}

static void record_reput(trace_recorder_t *rec, jit_event_t *e)
{
    //    _PUSH(_POP());
}

static void record_topn(trace_recorder_t *rec, jit_event_t *e)
{
    //    lir_t Rval;
    //    rb_num_t n = (rb_num_t)GET_OPERAND(1);
    //    assert(0 && "need to test");
    //    Rval = _TOPN(n);
    //    _PUSH(Rval);
}

static void record_setn(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t n = (rb_num_t)GET_OPERAND(1);
    //    lir_t Rval = _POP();
    //    _SET(n, Rval);
    //    _PUSH(Rval);
}

static void record_adjuststack(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_num_t i, n = (rb_num_t)GET_OPERAND(1);
    //    for (i = 0; i < n; i++) {
    //        _POP();
    //    }
}

static void record_defined(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "defined");
}

static void record_checkmatch(trace_recorder_t *rec, jit_event_t *e)
{
    //    lir_t Rpattern = _POP();
    //    lir_t Rtarget = _POP();
    //    rb_event_flag_t flag = (rb_event_flag_t)GET_OPERAND(1);
    //    enum vm_check_match_type checkmatch_type
    //        = (enum vm_check_match_type)(flag & VM_CHECKMATCH_TYPE_MASK);
    //    if (flag & VM_CHECKMATCH_ARRAY) {
    //        _PUSH(EmitIR(PatternMatchRange, Rpattern, Rtarget, checkmatch_type));
    //    } else {
    //        _PUSH(EmitIR(PatternMatch, Rpattern, Rtarget, checkmatch_type));
    //    }
}

static void record_trace(trace_recorder_t *rec, jit_event_t *e)
{
    //    rb_event_flag_t flag = (rb_event_flag_t)GET_OPERAND(1);
    //    EmitIR(Trace, flag);
}

static void record_defineclass(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "defineclass");
}

static void record_send(trace_recorder_t *rec, jit_event_t *e)
{
    //    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    //    lir_t Rblock = 0;
    //    rb_block_t *block = NULL;
    //    ci->argc = ci->orig_argc;
    //    ci->blockptr = 0;
    //    // vm_caller_setup_args(th, reg_cfp, ci);
    //    if (UNLIKELY(ci->flag & VM_CALL_ARGS_BLOCKARG)) {
    //        not_support_op(rec, reg_cfp, reg_pc, "send");
    //        return;
    //    } else if (ci->blockiseq != 0) {
    //        ci->blockptr = RUBY_VM_GET_BLOCK_PTR_IN_CFP(reg_cfp);
    //        ci->blockptr->iseq = ci->blockiseq;
    //        ci->blockptr->proc = 0;
    //        Rblock = EmitIR(LoadSelfAsBlock, ci->blockiseq);
    //        block = ci->blockptr;
    //    }
    //
    //    if (UNLIKELY(ci->flag & VM_CALL_ARGS_SPLAT)) {
    //        not_support_op(rec, reg_cfp, reg_pc, "send");
    //        return;
    //    }
    //
    //    TakeStackSnapshot(rec, reg_pc);
    //    EmitMethodCall(rec, reg_cfp, reg_pc, ci, block, Rblock, BIN(send));
}

static void record_opt_str_freeze(trace_recorder_t *rec, jit_event_t *e)
{
    //    assert(0 && "need to test");
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_str_freeze));
}

static void record_opt_send_simple(trace_recorder_t *rec, jit_event_t *e)
{
    //    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    //    TakeStackSnapshot(rec, reg_pc);
    //    EmitMethodCall(rec, reg_cfp, reg_pc, ci, 0, 0, BIN(opt_send_simple));
}

static void record_invokesuper(trace_recorder_t *rec, jit_event_t *e)
{
    //    //CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    //    TakeStackSnapshot(rec, reg_pc);
    //    //ci->argc = ci->orig_argc;
    //    //ci->blockptr = !(ci->flag & VM_CALL_ARGS_BLOCKARG) ? GET_BLOCK_PTR() : 0;
    //
    //    //if (UNLIKELY(!(ci->flag & VM_CALL_ARGS_SKIP_SETUP))) {
    //    //    vm_caller_setup_args(th, reg_cfp, ci);
    //    //}
    //    //ci->recv = GET_SELF();
    //    //vm_search_super_method(th, GET_CFP(), ci);
    //    //CALL_METHOD(ci);
    //
    //    not_support_op(rec, reg_cfp, reg_pc, "invokesuper");
}

static void record_invokeblock(trace_recorder_t *rec, jit_event_t *e)
{
    //    const rb_block_t* block;
    //    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    //    int i, argc = 1 /*recv*/ + ci->orig_argc;
    //    lir_t Rblock;
    //    lir_t regs[ci->orig_argc + 1];
    //    VALUE params[ci->orig_argc + 1];
    //
    //    VALUE type;
    //
    //    TakeStackSnapshot(rec, reg_pc);
    //
    //    block = rb_vm_control_frame_block_ptr(reg_cfp);
    //    regs[0] = EmitIR(LoadSelf);
    //    Rblock = EmitIR(LoadBlock, block->iseq);
    //
    //    ci->argc = ci->orig_argc;
    //    ci->blockptr = 0;
    //    ci->recv = GET_SELF();
    //
    //    // fprintf(stderr, "cfp=%p, block=%p\n", reg_cfp, block);
    //    type = GET_ISEQ()->local_iseq->type;
    //
    //    if ((type != ISEQ_TYPE_METHOD && type != ISEQ_TYPE_CLASS) || block == 0) {
    //        // "no block given (yield)"
    //        TracerecorderAbort(rec, reg_cfp, reg_pc, TRACE_ERROR_THROW);
    //        return;
    //    }
    //
    //    if (UNLIKELY(ci->flag & VM_CALL_ARGS_SPLAT)) {
    //        TracerecorderAbort(rec, reg_cfp, reg_pc, TRACE_ERROR_UNSUPPORT_OP);
    //        return;
    //    }
    //
    //    if (BUILTIN_TYPE(block->iseq) == T_NODE) {
    //        // yield native block
    //        TracerecorderAbort(rec, reg_cfp, reg_pc, TRACE_ERROR_NATIVE_METHOD);
    //        return;
    //    }
    //
    //    rec->CallDepth += 1;
    //
    //    for (i = 0; i < ci->orig_argc; i++) {
    //        params[i] = TOPN(ci->orig_argc - i);
    //        regs[ci->orig_argc - i] = _POP();
    //    }
    //
    //    EmitIR(GuardBlockEqual, reg_pc, Rblock, (VALUE)block);
    //    PushCallStack(rec, argc, regs, 0);
    //    _PUSH(EmitIR(FramePush, reg_pc, ci, 1, Rblock, argc, regs));
    //
    //    //EmitJump(rec, reg_pc, 0);
}

static void record_leave(trace_recorder_t *rec, jit_event_t *e)
{
    //    lir_t Val;
    //    if (VM_FRAME_TYPE_FINISH_P(reg_cfp)) {
    //        TracerecorderAbort(rec, reg_cfp, reg_pc, TRACE_ERROR_LEAVE);
    //        return;
    //    }
    //    if (rec->CallDepth == 0) {
    //        TracerecorderAbort(rec, reg_cfp, reg_pc, TRACE_ERROR_LEAVE);
    //        return;
    //    }
    //    rec->CallDepth -= 1;
    //    Val = _POP();
    //    PopCallStack(rec);
    //
    //    EmitIR(FramePop);
    //    //EmitJump(rec, reg_pc, 0);
    //    _PUSH(Val);
}

static void record_throw(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "throw");
}

static void record_jump(trace_recorder_t *rec, jit_event_t *e)
{
    //    OFFSET dst = (OFFSET)GET_OPERAND(1);
    //    VALUE *TargetPC = reg_pc + insn_len(BIN(jump)) + dst;
    //    EmitJump(rec, TargetPC, 1);
}

static void record_branchif(trace_recorder_t *rec, jit_event_t *e)
{
    //    OFFSET dst = (OFFSET)GET_OPERAND(1);
    //    lir_t Rval = _POP();
    //    VALUE val = TOPN(0);
    //    VALUE *NextPC = reg_pc + insn_len(BIN(branchif));
    //    VALUE *JumpPC = NextPC + dst;
    //    trace_mode_t prev_mode = RJitGetMode(rec->jit);
    //
    //    if (JumpPC == TracerecorderGetTrace(rec)->LastPC) {
    //        RJitSetMode(rec->jit, prev_mode | TRACE_MODE_EMIT_BACKWARD_BRANCH);
    //    }
    //    if (RTEST(val)) {
    //        TakeStackSnapshot(rec, NextPC);
    //        EmitIR(GuardTypeNil, NextPC, Rval);
    //        EmitJump(rec, JumpPC, 1);
    //    } else {
    //        TakeStackSnapshot(rec, JumpPC);
    //        EmitIR(GuardTypeNil, JumpPC, Rval);
    //        EmitJump(rec, NextPC, 1);
    //    }
    //    if (JumpPC == TracerecorderGetTrace(rec)->LastPC) {
    //        RJitSetMode(rec->jit, prev_mode);
    //    }
}

static void record_branchunless(trace_recorder_t *rec, jit_event_t *e)
{
    //    OFFSET dst = (OFFSET)GET_OPERAND(1);
    //    lir_t Rval = _POP();
    //    VALUE val = TOPN(0);
    //    VALUE *NextPC = reg_pc + insn_len(BIN(branchunless));
    //    VALUE *JumpPC = NextPC + dst;
    //    VALUE *TargetPC = NULL;
    //
    //    if (!RTEST(val)) {
    //        TakeStackSnapshot(rec, NextPC);
    //        EmitIR(GuardTypeNonNil, NextPC, Rval);
    //        TargetPC = JumpPC;
    //    } else {
    //        TakeStackSnapshot(rec, JumpPC);
    //        EmitIR(GuardTypeNil, JumpPC, Rval);
    //        TargetPC = NextPC;
    //    }
    //
    //    EmitJump(rec, TargetPC, 1);
}

static void record_getinlinecache(trace_recorder_t *rec, jit_event_t *e)
{
    //    IC ic = (IC)GET_OPERAND(2);
    //    if (ic->ic_serial != GET_GLOBAL_CONSTANT_STATE()) {
    //        // hmm, constant value is re-defined.
    //        not_support_op(rec, reg_cfp, reg_pc, "getinlinecache");
    //        return;
    //    }
    //    _PUSH(EmitLoadConst(rec, ic->ic_value.value));
}

static void record_setinlinecache(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "setinlinecache");
}

static void record_once(trace_recorder_t *rec, jit_event_t *e)
{
    //    IC ic = (IC)GET_OPERAND(2);
    //    //ISEQ iseq = (ISEQ)GET_OPERAND(1);
    //    union iseq_inline_storage_entry *is = (union iseq_inline_storage_entry *)ic;
    //
    //    if (is->once.done == Qfalse) {
    //        not_support_op(rec, reg_cfp, reg_pc, "once");
    //    } else {
    //        _PUSH(EmitLoadConst(rec, is->once.value));
    //    }
}

static void record_opt_case_dispatch(trace_recorder_t *rec, jit_event_t *e)
{
    //  lir_t Rkey = _POP();
    //  OFFSET else_offset = (OFFSET)GET_OPERAND(2);
    //  CDHASH hash = (CDHASH)GET_OPERAND(1);
    //  VALUE key = TOPN(0);
    //
    //  TakeStackSnapshot(rec, reg_pc);
    //  int type = TYPE(key);
    //  switch(type) {
    //    case T_FLOAT: {
    //      // FIXME
    //      not_support_op(rec, reg_cfp, reg_pc, "opt_case_dispatch");
    //      //double ival;
    //      //if (modf(RFLOAT_VALUE(key), &ival) == 0.0) {
    //      //  key = FIXABLE(ival) ? LONG2FIX((long)ival) : rb_dbl2big(ival);
    //      //}
    //    }
    //    case T_SYMBOL: /* fall through */
    //    case T_FIXNUM:
    //    case T_BIGNUM:
    //    case T_STRING:
    //      if (BASIC_OP_UNREDEFINED_P(BOP_EQQ,
    //                                 SYMBOL_REDEFINED_OP_FLAG |
    //                                 FIXNUM_REDEFINED_OP_FLAG |
    //                                 BIGNUM_REDEFINED_OP_FLAG |
    //                                 STRING_REDEFINED_OP_FLAG)) {
    //        if (type == T_SYMBOL) {
    //          EmitIR(GuardTypeSymbol, reg_pc, Rkey);
    //          EmitIR(GuardMethodRedefine, reg_pc, SYMBOL_REDEFINED_OP_FLAG, BOP_EQQ);
    //        }
    //        else if (type == T_FIXNUM) {
    //          EmitIR(GuardTypeFixnum, reg_pc, Rkey);
    //          EmitIR(GuardMethodRedefine, reg_pc, FIXNUM_REDEFINED_OP_FLAG, BOP_EQQ);
    //        }
    //        else if (type == T_BIGNUM) {
    //          EmitIR(GuardTypeBignum, reg_pc, Rkey);
    //          EmitIR(GuardMethodRedefine, reg_pc, BIGNUM_REDEFINED_OP_FLAG, BOP_EQQ);
    //        }
    //        else if (type == T_STRING) {
    //          EmitIR(GuardTypeString, reg_pc, Rkey);
    //          EmitIR(GuardMethodRedefine, reg_pc, STRING_REDEFINED_OP_FLAG, BOP_EQQ);
    //        }
    //        st_data_t val;
    //        // We assume `hash` is constant variable
    //        if (st_lookup(RHASH_TBL_RAW(hash), key, &val)) {
    //          VALUE *dst = reg_pc + insn_len(BIN(opt_case_dispatch))
    //              + FIX2INT((VALUE)val);
    //          EmitJump(rec, dst, 1);
    //        }
    //        else {
    //          VALUE *dst = reg_pc + insn_len(BIN(opt_case_dispatch))
    //              + else_offset;
    //          JUMP(else_offset);
    //          EmitJump(rec, dst, 1);
    //        }
    //        break;
    //      }
    //    default:
    //      break;
    //  }
}

static void record_opt_plus(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_plus));
}

static void record_opt_minus(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_minus));
}

static void record_opt_mult(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_mult));
}

static void record_opt_div(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_div));
}

static void record_opt_mod(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_mod));
}

static void record_opt_eq(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_eq));
}

static void record_opt_neq(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_neq));
}

static void record_opt_lt(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_lt));
}

static void record_opt_le(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_le));
}

static void record_opt_gt(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_gt));
}

static void record_opt_ge(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_ge));
}

static void record_opt_ltlt(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_ltlt));
}

static void record_opt_aref(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_aref));
}

static void record_opt_aset(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_aset));
}

static void record_opt_aset_with(trace_recorder_t *rec, jit_event_t *e)
{
    //    VALUE recv, key, val;
    //    lir_t Robj, Rrecv, Rval;
    //    CALL_INFO ci;
    //    VALUE params[3];
    //    lir_t regs[3];
    //
    //    TakeStackSnapshot(rec, reg_pc);
    //    ci = (CALL_INFO)GET_OPERAND(1);
    //    key = (VALUE)GET_OPERAND(2);
    //    recv = TOPN(1);
    //    val = TOPN(0);
    //    Rval = _POP();
    //    Rrecv = _POP();
    //    Robj = EmitLoadConst(rec, key);
    //
    //    params[0] = recv;
    //    params[1] = key;
    //    params[2] = val;
    //    regs[0] = Rrecv;
    //    regs[1] = Robj;
    //    regs[2] = Rval;
    //    EmitSpecialInst0(rec, reg_pc, ci, BIN(opt_aset_with), params, regs);
}

static void record_opt_aref_with(trace_recorder_t *rec, jit_event_t *e)
{
    //    VALUE recv, key;
    //    lir_t Robj, Rrecv;
    //    CALL_INFO ci;
    //    VALUE params[2];
    //    lir_t regs[2];
    //    TakeStackSnapshot(rec, reg_pc);
    //    ci = (CALL_INFO)GET_OPERAND(1);
    //    key = (VALUE)GET_OPERAND(2);
    //    recv = TOPN(0);
    //    Rrecv = _POP();
    //    Robj = EmitLoadConst(rec, key);
    //
    //    params[0] = recv;
    //    params[1] = key;
    //    regs[0] = Rrecv;
    //    regs[1] = Robj;
    //    EmitSpecialInst0(rec, reg_pc, ci, BIN(opt_aref_with), params, regs);
}

static void record_opt_length(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_length));
}

static void record_opt_size(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_size));
}

static void record_opt_empty_p(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_empty_p));
}

static void record_opt_succ(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_succ));
}

static void record_opt_not(trace_recorder_t *rec, jit_event_t *e)
{
    //    EmitSpecialInst1(rec, reg_cfp, reg_pc, BIN(opt_not));
}

static void record_opt_regexpmatch1(trace_recorder_t *rec, jit_event_t *e)
{
    //    VALUE params[2];
    //    lir_t regs[2];
    //    VALUE obj, r;
    //    lir_t RRe, Robj;
    //    rb_call_info_t ci;
    //
    //    TakeStackSnapshot(rec, reg_pc);
    //    obj = TOPN(0);
    //    r = GET_OPERAND(1);
    //    RRe = EmitLoadConst(rec, r);
    //    Robj = _POP();
    //    ci.mid = idEqTilde;
    //    ci.flag = 0;
    //    ci.orig_argc = ci.argc = 1;
    //
    //    params[0] = obj;
    //    params[1] = r;
    //    regs[0] = Robj;
    //    regs[1] = RRe;
    //    EmitSpecialInst0(rec, reg_pc, &ci, BIN(opt_regexpmatch1), params, regs);
}

static void record_opt_regexpmatch2(trace_recorder_t *rec, jit_event_t *e)
{
    //    VALUE params[2];
    //    lir_t regs[2];
    //    VALUE obj1, obj2;
    //    lir_t Robj1, Robj2;
    //    rb_call_info_t ci;
    //
    //    TakeStackSnapshot(rec, reg_pc);
    //    obj2 = TOPN(1);
    //    obj1 = TOPN(0);
    //    Robj1 = _POP();
    //    Robj2 = _POP();
    //    ci.mid = idEqTilde;
    //    ci.flag = 0;
    //    ci.orig_argc = ci.argc = 1;
    //    params[0] = obj2;
    //    params[1] = obj1;
    //    regs[0] = Robj2;
    //    regs[1] = Robj1;
    //    EmitSpecialInst0(rec, reg_pc, &ci, BIN(opt_regexpmatch2), params, regs);
}

static void record_opt_call_c_function(trace_recorder_t *rec, jit_event_t *e)
{
    //    not_support_op(rec, reg_cfp, reg_pc, "opt_call_c_function");
}

static void record_bitblt(trace_recorder_t *rec, jit_event_t *e)
{
    //    VALUE str = rb_str_new2("a bit of bacon, lettuce and tomato");
    //    _PUSH(EmitLoadConst(rec, str));
}

static void record_answer(trace_recorder_t *rec, jit_event_t *e)
{
    //    _PUSH(EmitLoadConst(rec, INT2FIX(42)));
}

static void record_getlocal_OP__WC__0(trace_recorder_t *rec, jit_event_t *e)
{
    //    lindex_t idx = (lindex_t)GET_OPERAND(1);
    //    _record_getlocal(rec, 0, idx);
}

static void record_getlocal_OP__WC__1(trace_recorder_t *rec, jit_event_t *e)
{
    //    lindex_t idx = (lindex_t)GET_OPERAND(1);
    //    _record_getlocal(rec, 1, idx);
}

static void record_setlocal_OP__WC__0(trace_recorder_t *rec, jit_event_t *e)
{
    //    lindex_t idx = (lindex_t)GET_OPERAND(1);
    //    _record_setlocal(rec, 0, idx);
}

static void record_setlocal_OP__WC__1(trace_recorder_t *rec, jit_event_t *e)
{
    //    lindex_t idx = (lindex_t)GET_OPERAND(1);
    //    _record_setlocal(rec, 1, idx);
}

static void record_putobject_OP_INT2FIX_O_0_C_(trace_recorder_t *rec, jit_event_t *e)
{
    //    lir_t Rval = EmitLoadConst(rec, INT2FIX(0));
    //    _PUSH(Rval);
}

static void record_putobject_OP_INT2FIX_O_1_C_(trace_recorder_t *rec, jit_event_t *e)
{
    //    lir_t Rval = EmitLoadConst(rec, INT2FIX(1));
    //    _PUSH(Rval);
}

static void record_insn(trace_recorder_t *rec, jit_event_t *e)
{
    int opcode = e->opcode;

#define CASE_RECORD(op)      \
    case BIN(op):            \
	record_##op(rec, e); \
	break
    switch (opcode) {
	CASE_RECORD(nop);
	CASE_RECORD(getlocal);
	CASE_RECORD(setlocal);
	CASE_RECORD(getspecial);
	CASE_RECORD(setspecial);
	CASE_RECORD(getinstancevariable);
	CASE_RECORD(setinstancevariable);
	CASE_RECORD(getclassvariable);
	CASE_RECORD(setclassvariable);
	CASE_RECORD(getconstant);
	CASE_RECORD(setconstant);
	CASE_RECORD(getglobal);
	CASE_RECORD(setglobal);
	CASE_RECORD(putnil);
	CASE_RECORD(putself);
	CASE_RECORD(putobject);
	CASE_RECORD(putspecialobject);
	CASE_RECORD(putiseq);
	CASE_RECORD(putstring);
	CASE_RECORD(concatstrings);
	CASE_RECORD(tostring);
	CASE_RECORD(toregexp);
	CASE_RECORD(newarray);
	CASE_RECORD(duparray);
	CASE_RECORD(expandarray);
	CASE_RECORD(concatarray);
	CASE_RECORD(splatarray);
	CASE_RECORD(newhash);
	CASE_RECORD(newrange);
	CASE_RECORD(pop);
	CASE_RECORD(dup);
	CASE_RECORD(dupn);
	CASE_RECORD(swap);
	CASE_RECORD(reput);
	CASE_RECORD(topn);
	CASE_RECORD(setn);
	CASE_RECORD(adjuststack);
	CASE_RECORD(defined);
	CASE_RECORD(checkmatch);
	CASE_RECORD(trace);
	CASE_RECORD(defineclass);
	CASE_RECORD(send);
	CASE_RECORD(opt_str_freeze);
	CASE_RECORD(opt_send_simple);
	CASE_RECORD(invokesuper);
	CASE_RECORD(invokeblock);
	CASE_RECORD(leave);
	CASE_RECORD(throw);
	CASE_RECORD(jump);
	CASE_RECORD(branchif);
	CASE_RECORD(branchunless);
	CASE_RECORD(getinlinecache);
	CASE_RECORD(setinlinecache);
	CASE_RECORD(once);
	CASE_RECORD(opt_case_dispatch);
	CASE_RECORD(opt_plus);
	CASE_RECORD(opt_minus);
	CASE_RECORD(opt_mult);
	CASE_RECORD(opt_div);
	CASE_RECORD(opt_mod);
	CASE_RECORD(opt_eq);
	CASE_RECORD(opt_neq);
	CASE_RECORD(opt_lt);
	CASE_RECORD(opt_le);
	CASE_RECORD(opt_gt);
	CASE_RECORD(opt_ge);
	CASE_RECORD(opt_ltlt);
	CASE_RECORD(opt_aref);
	CASE_RECORD(opt_aset);
	CASE_RECORD(opt_aset_with);
	CASE_RECORD(opt_aref_with);
	CASE_RECORD(opt_length);
	CASE_RECORD(opt_size);
	CASE_RECORD(opt_empty_p);
	CASE_RECORD(opt_succ);
	CASE_RECORD(opt_not);
	CASE_RECORD(opt_regexpmatch1);
	CASE_RECORD(opt_regexpmatch2);
	CASE_RECORD(opt_call_c_function);
	CASE_RECORD(bitblt);
	CASE_RECORD(answer);
	CASE_RECORD(getlocal_OP__WC__0);
	CASE_RECORD(getlocal_OP__WC__1);
	CASE_RECORD(setlocal_OP__WC__0);
	CASE_RECORD(setlocal_OP__WC__1);
	CASE_RECORD(putobject_OP_INT2FIX_O_0_C_);
	CASE_RECORD(putobject_OP_INT2FIX_O_1_C_);
	default:
	    assert(0 && "unreachable");
    }
}
