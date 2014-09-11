/**********************************************************************

  optimizer.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

static lir_t EmitLoadConst(trace_recorder_t *rec, VALUE val);

typedef VALUE (*lir_folder1_t)(VALUE);
typedef VALUE (*lir_folder2_t)(VALUE, VALUE);

static void lir_inst_replace_with(trace_recorder_t *rec, lir_inst_t *inst, lir_inst_t *newinst)
{
    unsigned i;
    int j;
    hashmap_iterator_t itr = { 0, 0 };
    lir_inst_t **ref = NULL;
    lir_inst_t *user;
    // 1. replace argument of inst->user->list[x]
    if (inst->user) {
	for (i = 0; i < inst->user->size; i++) {
	    j = 0;
	    user = (lir_t)jit_list_get(inst->user, i);
	    while ((ref = lir_inst_get_args(user, j)) != NULL) {
		if (ref && *ref == inst) {
		    *ref = newinst;
		}
		j += 1;
	    }
	}
    }

    // 2. replace side exit
    while (hashmap_next(&rec->stack_map, &itr)) {
	regstack_t *stack = (regstack_t *)itr.entry->val;
	for (i = 0; i < stack->list.size; i++) {
	    if (inst == regstack_get_direct(stack, i)) {
		regstack_set_direct(stack, i, newinst);
	    }
	}
    }

    newinst->user = inst->user;
    inst->user = NULL;
}

static int lir_inst_define_value(int opcode)
{
    switch (opcode) {
#define DEF_IR(OPNAME)     \
    case OPCODE_I##OPNAME: \
	return LIR_USE_##OPNAME;

	LIR_EACH(DEF_IR);
	default:
	    assert(0 && "unreachable");
    }
#undef DEF_IR
    return 0;
}

static int lir_inst_have_side_effect(int opcode)
{
#define DEF_SIDE_EFFECT(OPNAME) \
    case OPCODE_I##OPNAME:      \
	return LIR_SIDE_EFFECT_##OPNAME;
    switch (opcode) {
	LIR_EACH(DEF_SIDE_EFFECT);
	default:
	    assert(0 && "unreachable");
    }
#undef DEF_SIDE_EFFECT
    return 0;
}

static int is_constant(lir_inst_t *inst)
{
    switch (lir_opcode(inst)) {
	case OPCODE_ILoadConstNil:
	case OPCODE_ILoadConstObject:
	case OPCODE_ILoadConstBoolean:
	case OPCODE_ILoadConstFixnum:
	case OPCODE_ILoadConstFloat:
	case OPCODE_ILoadConstString:
	case OPCODE_ILoadConstRegexp:
	    return 1;
	default:
	    break;
    }
    return 0;
}

static int elimnate_guard(trace_recorder_t *rec, lir_inst_t *inst)
{
    /* Remove guard that always true
     * e.g.) L2 is always true becuase L1 is fixnum. So we can remove L2.
     * L1 = LoadConstFixnum 10
     * L2 = GuardTypeFixnum L1 exit_pc
     */
    IGuardTypeFixnum *guard = (IGuardTypeFixnum *)inst;
    lir_inst_t *src = guard->R;

    if (src == NULL) {
	return 0;
    }

#define RETURN_IF(INST1, OP1, INST2, OP2)         \
    if (lir_opcode(INST1) == OPCODE_I##OP1) {     \
	if (lir_opcode(INST2) == OPCODE_I##OP2) { \
	    return 1;                             \
	}                                         \
    }

    RETURN_IF(inst, GuardTypeFixnum, src, LoadConstFixnum);
    RETURN_IF(inst, GuardTypeFloat, src, LoadConstFloat);
    RETURN_IF(inst, GuardTypeRegexp, src, LoadConstRegexp);

    if (lir_opcode(inst) == OPCODE_IGuardTypeSymbol) {
	if (lir_opcode(src) == OPCODE_ILoadConstObject) {
	    if (SYMBOL_P(((ILoadConstObject *)src)->Val)) {
		return 1;
	    }
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardTypeFloat) {
	if (lir_opcode(src) == OPCODE_ILoadConstFloat) {
	    if (RB_FLOAT_TYPE_P(((ILoadConstFloat *)src)->Val)) {
		return 1;
	    }
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardTypeArray) {
	if (lir_opcode(src) == OPCODE_IAllocArray) {
	    return 1;
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardTypeString) {
	if (lir_opcode(src) == OPCODE_ILoadConstString) {
	    return 1;
	}
	if (lir_opcode(src) == OPCODE_IAllocString) {
	    return 1;
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardTypeSpecialConst) {
	switch (lir_opcode(src)) {
	    case OPCODE_ILoadConstNil:
	    case OPCODE_ILoadConstBoolean:
	    case OPCODE_ILoadConstFixnum:
		return 0;
	    case OPCODE_ILoadConstFloat:
		if (!RB_FLOAT_TYPE_P(((ILoadConstFloat *)src)->Val)) {
		    return 1;
		}
	    case OPCODE_ILoadConstObject:
	    case OPCODE_ILoadConstString:
	    case OPCODE_ILoadConstRegexp:
		return 1;
	    default:
		break;
	}
    }

    return 0;
}

static lir_inst_t *fold_binop_fixnum2(trace_recorder_t *rec, lir_folder_t folder, lir_inst_t *inst)
{
    ILoadConstFixnum *LHS = (ILoadConstFixnum *)*lir_inst_get_args(inst, 0);
    ILoadConstFixnum *RHS = (ILoadConstFixnum *)*lir_inst_get_args(inst, 1);
    int lop = lir_opcode(&LHS->base);
    int rop = lir_opcode(&RHS->base);
    // const + const
    if (lop == OPCODE_ILoadConstFixnum && rop == OPCODE_ILoadConstFixnum) {
	VALUE val = ((lir_folder2_t)folder)(LHS->Val, RHS->Val);
	return EmitLoadConst(rec, val);
    }
    return inst;
}

static lir_inst_t *fold_binop_float2(trace_recorder_t *rec, lir_folder_t folder, lir_inst_t *inst)
{
    ILoadConstFloat *LHS = (ILoadConstFloat *)*lir_inst_get_args(inst, 0);
    ILoadConstFloat *RHS = (ILoadConstFloat *)*lir_inst_get_args(inst, 1);
    int lop = lir_opcode(&LHS->base);
    int rop = lir_opcode(&RHS->base);
    // const + const
    if (lop == OPCODE_ILoadConstFloat && rop == OPCODE_ILoadConstFloat) {
	// FIXME need to insert GuardTypeFlonum?
	VALUE val = ((lir_folder2_t)folder)(LHS->Val, RHS->Val);
	return EmitLoadConst(rec, val);
    }
    return inst;
}

static lir_inst_t *remove_overflow_check(trace_recorder_t *rec, lir_inst_t *inst)
{
    IFixnumAdd *ir = (IFixnumAdd *)inst;
    switch (lir_opcode(inst)) {
	case OPCODE_IFixnumAdd:
	    return Emit_FixnumAdd(rec, ir->LHS, ir->RHS);
	case OPCODE_IFixnumSub:
	    return Emit_FixnumSub(rec, ir->LHS, ir->RHS);
	case OPCODE_IFixnumMul:
	    return Emit_FixnumMul(rec, ir->LHS, ir->RHS);
	case OPCODE_IFixnumDiv:
	    return Emit_FixnumDiv(rec, ir->LHS, ir->RHS);
	case OPCODE_IFixnumMod:
	    return Emit_FixnumMod(rec, ir->LHS, ir->RHS);
	default:
	    break;
    }
    return inst;
}

static lir_inst_t *fold_binop_cast(trace_recorder_t *rec, lir_folder_t folder, lir_inst_t *inst)
{
    ILoadConstObject *Val = (ILoadConstObject *)*lir_inst_get_args(inst, 0);
    int vopcode = lir_opcode(&Val->base);
    VALUE val = Qundef;
    switch (lir_opcode(inst)) {
	case OPCODE_IFixnumToFloat:
	case OPCODE_IFixnumToString:
	    if (vopcode == OPCODE_ILoadConstFixnum) {
		val = ((lir_folder1_t)folder)(Val->Val);
		return EmitLoadConst(rec, val);
	    }
	    break;
	case OPCODE_IFloatToFixnum:
	case OPCODE_IFloatToString:
	    if (vopcode == OPCODE_ILoadConstFloat) {
		val = ((lir_folder1_t)folder)(Val->Val);
		return EmitLoadConst(rec, val);
	    }
	    break;
	case OPCODE_IStringToFixnum:
	case OPCODE_IStringToFloat:
	    if (vopcode == OPCODE_ILoadConstFloat) {
		val = ((lir_folder1_t)folder)(Val->Val);
		return EmitLoadConst(rec, val);
	    }
	    break;
	case OPCODE_IObjectToString:
	    switch (vopcode) {
		case OPCODE_ILoadConstNil:
		case OPCODE_ILoadConstObject:
		case OPCODE_ILoadConstBoolean:
		case OPCODE_ILoadConstFixnum:
		case OPCODE_ILoadConstFloat:
		case OPCODE_ILoadConstString:
		case OPCODE_ILoadConstRegexp:
		    val = ((lir_folder1_t)folder)(Val->Val);
		    return EmitLoadConst(rec, val);
		default:
		    break;
	    }
	    break;
    }
    return inst;
}

static lir_inst_t *fold_string_add(trace_recorder_t *rec, lir_folder_t folder, lir_inst_t *inst)
{
#if 1
    lir_inst_t *LHS = *lir_inst_get_args(inst, 0);
    lir_inst_t *RHS = *lir_inst_get_args(inst, 1);
    assert(folder == rb_jit_exec_IStringAdd);
    // (StringAdd (AllocStr LoadConstString1) LoadConstString2)
    // => (AllocStr LoadConstString3)
    if (lir_opcode(LHS) == OPCODE_IAllocString) {
	IAllocString *left = (IAllocString *)LHS;
	if (lir_opcode(left->OrigStr) == OPCODE_ILoadConstString) {
	    ILoadConstString *lstr = (ILoadConstString *)left->OrigStr;
	    if (lir_opcode(RHS) == OPCODE_ILoadConstString) {
		ILoadConstString *rstr = (ILoadConstString *)RHS;
		VALUE val = rb_str_plus(lstr->Val, rstr->Val);
		lir_inst_t *tmp = EmitLoadConst(rec, val);
		return Emit_AllocString(rec, tmp);
	    }
	}
    }
#endif
    return inst;
}

static lir_inst_t *add_const_pool(trace_recorder_t *rec, lir_inst_t *inst)
{
    VALUE val;
    if (lir_opcode(inst) == OPCODE_ILoadConstNil) {
	val = Qnil;
    }
    else {
	val = ((ILoadConstObject *)inst)->Val;
    }
    return trace_recorder_add_const(rec, val, inst);
}

static lir_inst_t *constant_fold_inst(trace_recorder_t *rec, lir_inst_t *inst)
{
    lir_folder_t folder;
    if (lir_is_guard(inst) || lir_is_terminator(inst)) {
	return inst;
    }
    if (is_constant(inst)) {
	return inst;
    }
    folder = const_fold_funcs[lir_opcode(inst)];
    if (folder == NULL) {
	return inst;
    }

    switch (lir_opcode(inst)) {
	case OPCODE_IFixnumToFloat:
	case OPCODE_IFixnumToString:
	case OPCODE_IFloatToFixnum:
	case OPCODE_IFloatToString:
	case OPCODE_IStringToFixnum:
	case OPCODE_IStringToFloat:
	case OPCODE_IObjectToString:
	    return fold_binop_cast(rec, folder, inst);

	//case OPCODE_IFixnumComplement :
	//case OPCODE_IMathSin :
	//case OPCODE_IMathCos :
	//case OPCODE_IMathTan :
	//case OPCODE_IMathExp :
	//case OPCODE_IMathSqrt :
	//case OPCODE_IMathLog10 :
	//case OPCODE_IMathLog2 :
	//case OPCODE_IStringLength :
	//case OPCODE_IStringEmptyP :
	//case OPCODE_IArrayLength :
	//case OPCODE_IArrayEmptyP :
	//case OPCODE_IArrayGet :
	//case OPCODE_IHashLength :
	//case OPCODE_IHashEmptyP :
	//case OPCODE_IHashGet :

	case OPCODE_IFixnumAdd:
	case OPCODE_IFixnumSub:
	case OPCODE_IFixnumMul:
	case OPCODE_IFixnumDiv:
	case OPCODE_IFixnumMod:
	case OPCODE_IFixnumAddOverflow:
	case OPCODE_IFixnumSubOverflow:
	case OPCODE_IFixnumMulOverflow:
	case OPCODE_IFixnumDivOverflow:
	case OPCODE_IFixnumModOverflow:
	case OPCODE_IFixnumEq:
	case OPCODE_IFixnumNe:
	case OPCODE_IFixnumGt:
	case OPCODE_IFixnumGe:
	case OPCODE_IFixnumLt:
	case OPCODE_IFixnumLe:
	case OPCODE_IFixnumAnd:
	case OPCODE_IFixnumOr:
	case OPCODE_IFixnumXor:
	case OPCODE_IFixnumLshift:
	case OPCODE_IFixnumRshift:
	    return fold_binop_fixnum2(rec, folder, inst);
	case OPCODE_IFloatAdd:
	case OPCODE_IFloatSub:
	case OPCODE_IFloatMul:
	case OPCODE_IFloatDiv:
	case OPCODE_IFloatMod:
	case OPCODE_IFloatEq:
	case OPCODE_IFloatNe:
	case OPCODE_IFloatGt:
	case OPCODE_IFloatGe:
	case OPCODE_IFloatLt:
	case OPCODE_IFloatLe:
	    return fold_binop_float2(rec, folder, inst);
	case OPCODE_IStringAdd:
	    return fold_string_add(rec, folder, inst);
	case OPCODE_IStringConcat:
	case OPCODE_IArrayConcat:
	case OPCODE_IRegExpMatch:
	    break;
	default:
	    break;
    }
    return inst;
}

typedef struct worklist_t {
    jit_list_t list;
    trace_recorder_t *rec;
} worklist_t;

typedef int (*worklist_func_t)(worklist_t *, lir_inst_t *);

static void worklist_push(worklist_t *list, lir_inst_t *ir)
{
    jit_list_add(&list->list, (uintptr_t)ir);
}

static lir_inst_t *worklist_pop(worklist_t *list)
{
    lir_inst_t *last;
    assert(list->list.size > 0);
    last = (lir_t)jit_list_get(&list->list, list->list.size - 1);
    list->list.size -= 1;
    return last;
}

static int worklist_empty(worklist_t *list)
{
    return list->list.size == 0;
}

static void worklist_init(worklist_t *list, jit_list_t *blocks, trace_recorder_t *rec)
{
    unsigned i, j;
    jit_list_init(&list->list);
    list->rec = rec;
    for (j = 0; j < blocks->size; j++) {
	basicblock_t *block = (basicblock_t *)jit_list_get(blocks, j);
	for (i = 0; i < block->insts.size; i++) {
	    lir_inst_t *inst = basicblock_get(block, i);
	    assert(inst != NULL);
	    worklist_push(list, inst);
	}
    }
}

static int apply_worklist(trace_recorder_t *rec, worklist_func_t func)
{
    int modified = 0;
    worklist_t worklist;
    worklist_init(&worklist, &rec->bblist, rec);
    while (!worklist_empty(&worklist)) {
	lir_inst_t *inst = worklist_pop(&worklist);
	modified += func(&worklist, inst);
    }
    return modified;
}

static int constant_fold(worklist_t *list, lir_inst_t *inst)
{
    unsigned i;
    lir_inst_t *newinst;
    trace_recorder_t *rec = list->rec;
    basicblock_t *prevBB = rec->cur_bb;
    rec->cur_bb = inst->parent;
    newinst = constant_fold_inst(rec, inst);
    rec->cur_bb = prevBB;
    if (inst != newinst) {
	if (inst->user) {
	    for (i = 0; i < inst->user->size; i++) {
		worklist_push(list, (lir_t)jit_list_get(inst->user, i));
	    }
	}
	lir_inst_replace_with(rec, inst, newinst);
	return 1;
    }
    return 0;
}

static int has_side_effect(lir_inst_t *inst)
{
    return lir_inst_have_side_effect(lir_opcode(inst));
}

static int need_by_side_exit(trace_recorder_t *rec, lir_inst_t *inst)
{
    hashmap_iterator_t itr = { 0, 0 };
    while (hashmap_next(&rec->stack_map, &itr)) {
	regstack_t *stack = (regstack_t *)itr.entry->val;
	unsigned i;
	for (i = 0; i < stack->list.size; i++) {
	    if (inst == regstack_get_direct(stack, i)) {
		return 1;
	    }
	}
    }
    return 0;
}

static int inst_is_dead(trace_recorder_t *rec, lir_inst_t *inst)
{
    if (inst->user && inst->user->size > 0) {
	return 0;
    }
    if (lir_is_terminator(inst)) {
	return 0;
    }
    if (lir_is_guard(inst)) {
	return 0;
    }
    if (has_side_effect(inst)) {
	return 0;
    }
    if (need_by_side_exit(rec, inst)) {
	return 0;
    }
    return 1;
}

static void remove_from_parent(lir_inst_t *inst)
{
    basicblock_t *bb = inst->parent;
    jit_list_remove(&bb->insts, (uintptr_t)inst);
}

static int eliminate_dead_code(worklist_t *list, lir_inst_t *inst)
{
    int i = 0;
    int is_dead = inst_is_dead(list->rec, inst);
    lir_inst_t **ref = NULL;
    if (!is_dead) {
	return 0;
    }
    while ((ref = lir_inst_get_args(inst, i)) != NULL) {
	if (*ref) {
	    lir_inst_removeuser(*ref, inst);
	    worklist_push(list, *ref);
	}
	i += 1;
    }

    remove_from_parent(inst);
    if (0) {
#if DUMP_LIR > 0
	dump_lir_inst(inst);
#endif
    }
    return 1;
}

static void trace_optimize(trace_recorder_t *rec, trace_t *trace)
{
    apply_worklist(rec, constant_fold);
    //loop_invariant_code_motion(rec);
    apply_worklist(rec, eliminate_dead_code);
}
