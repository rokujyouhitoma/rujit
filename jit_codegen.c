/**********************************************************************

  jit_codegen.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/
#define JIT_MAX_COMPILE_TRACE 10

typedef struct buffer {
    jit_list_t buf;
} buffer_t;

#define BUFFER_CAPACITY 4096

static buffer_t *buffer_init(buffer_t *buf)
{
    jit_list_init(&buf->buf);
    jit_list_ensure(&buf->buf, BUFFER_CAPACITY / sizeof(uintptr_t));
    return buf;
}

static void buffer_setnull(buffer_t *buf)
{
    unsigned size = buf->buf.size;
    unsigned capacity = buf->buf.capacity * sizeof(uintptr_t);
    assert(size < capacity);
    ((char *)buf->buf.list)[buf->buf.size] = '\0';
}

static int buffer_printf(buffer_t *buf, const char *fmt, va_list ap)
{
    unsigned size = buf->buf.size;
    unsigned capacity = buf->buf.capacity * sizeof(uintptr_t);
    char *ptr = ((char *)buf->buf.list) + size;
    size_t n = vsnprintf(ptr, capacity - size, fmt, ap);
    if (n + size < capacity) {
	buf->buf.size += n;
	return 1;
    }
    else {
	// data was not copied because buffer is full.
	buffer_setnull(buf);
	return 0;
    }
}

static void buffer_flush(FILE *fp, buffer_t *buf)
{
    fputs((char *)buf->buf.list, fp);
    buf->buf.size = 0;
}

static void buffer_dispose(buffer_t *buf)
{
    jit_list_delete(&buf->buf);
}

// cgen
enum cgen_mode {
    PROCESS_MODE, // generate native code directly
    FILE_MODE // generate temporary c-source file
};

typedef struct CGen {
    buffer_t buf;
    FILE *fp;
    void *hdr;
    const char *path;
    char *cmd;
    unsigned cmd_len;
    enum cgen_mode mode;
} CGen;

static void cgen_setup_command(CGen *gen, const char *lib, const char *file)
{
    gen->cmd_len = (unsigned)(strlen(cmd_template) + strlen(lib) + strlen(file));
    gen->cmd = (char *)malloc(gen->cmd_len + 1);
    memset(gen->cmd, 0, gen->cmd_len);
    snprintf(gen->cmd, gen->cmd_len, cmd_template, lib, file);
}

static void cgen_open(CGen *gen, enum cgen_mode mode, const char *path, int id)
{
    buffer_init(&gen->buf);
    gen->mode = mode;
    gen->hdr = NULL;
    JIT_PROFILE_ENTER("c-code generation");
    if (gen->mode == PROCESS_MODE) {
	cgen_setup_command(gen, path, "-");
	gen->fp = popen(gen->cmd, "w");
    }
    else {
	char fpath[512] = {};
	snprintf(fpath, 512, "/tmp/ruby_jit.%d.%d.c", getpid(), id);
	gen->fp = fopen(fpath, "w");
    }
    gen->path = path;
}

static int cgen_freeze(CGen *gen, int id)
{
    int success = 0;
    buffer_flush(gen->fp, &gen->buf);
    buffer_dispose(&gen->buf);
    JIT_PROFILE_LEAVE("c-code generation", JIT_DUMP_COMPILE_LOG > 0);
    JIT_PROFILE_ENTER("nativecode generation");
    if (gen->mode == FILE_MODE) {
	char fpath[512] = {};
	snprintf(fpath, 512, "/tmp/ruby_jit.%d.%d.c", getpid(), id);
	cgen_setup_command(gen, gen->path, fpath);

	if (JIT_DUMP_COMPILE_LOG > 1) {
	    fprintf(stderr, "compiling c code : %s\n", gen->cmd);
	}
	if (JIT_DUMP_COMPILE_LOG > 0) {
	    fprintf(stderr, "generated c-code is %s\n", gen->path);
	}
	fclose(gen->fp);
	gen->fp = popen(gen->cmd, "w");
    }
    success = pclose(gen->fp);
    JIT_PROFILE_LEAVE("nativecode generation", JIT_DUMP_COMPILE_LOG > 0);
    if (gen->cmd_len > 0) {
	gen->cmd_len = 0;
	free(gen->cmd);
	gen->cmd = NULL;
    }
    gen->fp = NULL;
    return success;
}

static void cgen_close(CGen *gen) { gen->hdr = NULL; }

static void cgen_printf(CGen *gen, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void cgen_printf(CGen *gen, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    if (buffer_printf(&gen->buf, fmt, ap) == 0) {
	buffer_flush(gen->fp, &gen->buf);
	vfprintf(gen->fp, fmt, ap2);
    }
    va_end(ap);
}

static void *cgen_get_function(CGen *gen, const char *fname, trace_t *trace)
{
    if (gen->hdr == NULL) {
	gen->hdr = dlopen(gen->path, RTLD_LAZY);
    }
    if (gen->hdr != NULL) {
	int (*finit)(const void *local_jit_runtime, trace_t *this_trace);
	char fname2[128] = {};
	snprintf(fname2, 128, "init_%s", fname);
	finit = dlsym(gen->hdr, fname2);
	if (finit) {
	    finit(&jit_runtime, trace);
	    trace->code = dlsym(gen->hdr, fname);
	    trace->handler = gen->hdr;
	}
    }
    return NULL;
}

static long get_sideexit_id(hashmap_t *sideexits, VALUE *pc)
{
    return hashmap_get(sideexits, (hashmap_data_t)pc) >> 1;
}

#define EMIT_CODE(GEN, OP, VAL, LHS, RHS)                         \
    cgen_printf(gen, "v%ld = rb_jit_exec_" #OP "(v%ld, v%ld);\n", \
                (VAL), lir_getid(LHS), lir_getid(RHS))

#define EMIT_CODE1(GEN, OP, VAL, ARG)                       \
    cgen_printf(gen, "v%ld = rb_jit_exec_" #OP "(v%ld);\n", \
                (VAL), lir_getid(ARG))

static void compile_inst(trace_recorder_t *Rec, CGen *gen, hashmap_t *SideExitBBs, lir_inst_t *Inst)
{
    long Id = lir_getid(Inst);
    switch (lir_opcode(Inst)) {
	case OPCODE_IGuardTypeSymbol: {
	    IGuardTypeSymbol *ir = (IGuardTypeSymbol *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!SYMBOL_P(v%ld)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeFixnum: {
	    IGuardTypeFixnum *ir = (IGuardTypeFixnum *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!FIXNUM_P(v%ld)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeBignum: {
	    IGuardTypeBignum *ir = (IGuardTypeBignum *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!RB_TYPE_P(v%ld, T_BIGNUM)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeFloat: {
	    IGuardTypeFloat *ir = (IGuardTypeFloat *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!(RB_FLOAT_TYPE_P(v%ld))) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeSpecialConst: {
	    IGuardTypeSpecialConst *ir = (IGuardTypeSpecialConst *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!!SPECIAL_CONST_P(v%ld)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeArray: {
	    IGuardTypeArray *ir = (IGuardTypeArray *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!(RBASIC_CLASS(v%ld) == local_jit_runtime->cArray)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeString: {
	    IGuardTypeString *ir = (IGuardTypeString *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!(RBASIC_CLASS(v%ld) == local_jit_runtime->cString)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeHash: {
	    IGuardTypeHash *ir = (IGuardTypeHash *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!(RBASIC_CLASS(v%ld) == local_jit_runtime->cHash)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeRegexp: {
	    IGuardTypeRegexp *ir = (IGuardTypeRegexp *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!(RBASIC_CLASS(v%ld) == local_jit_runtime->cRegexp)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeTime: {
	    IGuardTypeTime *ir = (IGuardTypeTime *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!(RBASIC_CLASS(v%ld) == local_jit_runtime->cTime)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeMath: {
	    IGuardTypeMath *ir = (IGuardTypeMath *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!(RBASIC_CLASS(v%ld) == local_jit_runtime->cMath)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeObject: {
	    IGuardTypeObject *ir = (IGuardTypeObject *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!(RB_TYPE_P(v%ld, T_OBJECT))) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeNil: {
	    IGuardTypeNil *ir = (IGuardTypeNil *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!RTEST(v%ld)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardTypeNonNil: {
	    IGuardTypeNonNil *ir = (IGuardTypeNonNil *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if(!!RTEST(v%ld)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardBlockEqual: {
	    IGuardBlockEqual *ir = (IGuardBlockEqual *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    rb_block_t *block = (rb_block_t *)ir->Block;
	    cgen_printf(gen, "{\n"
	                     "  rb_block_t *block = (rb_block_t *) v%ld;\n"
	                     "  const rb_iseq_t *iseq = (const rb_iseq_t *) %p;\n"
	                     "  if(!(block->iseq == iseq)) {\n"
	                     "    goto L_exit%ld;\n"
	                     "  }\n"
	                     "}\n",
	                lir_getid(ir->R), block->iseq, exit_block_id);
	    break;
	}
	case OPCODE_IGuardProperty: {
	    IGuardProperty *ir = (IGuardProperty *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    if (ir->is_attr) {
		CALL_INFO ci = (CALL_INFO)ir->cache;
		cgen_printf(gen, "if(!(%d > 0)) {\n"
		                 "  goto L_exit%ld;\n"
		                 "}\n",
		            ci->aux.index, exit_block_id);
	    }
	    else {
		IC ci = (IC)ir->cache;
		cgen_printf(
		    gen, "if(!(RCLASS_SERIAL(RBASIC(v%ld)->klass) == 0x%llx)) {\n"
		         "  goto L_exit%ld;\n"
		         "}\n",
		    lir_getid(ir->R), ci->ic_serial, exit_block_id);
	    }
	    break;
	}
	case OPCODE_IGuardMethodCache: {
	    IGuardMethodCache *ir = (IGuardMethodCache *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(
	        gen,
	        "{ CALL_INFO ci = (CALL_INFO) %p;\n"
	        "  if (!(JIT_GET_GLOBAL_METHOD_STATE()    == ci->method_state &&\n"
	        "       RCLASS_SERIAL(CLASS_OF(v%ld)) == ci->class_serial)) {\n"
	        "    goto L_exit%ld;\n"
	        "  }\n"
	        "}\n",
	        ir->ci, lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardMethodRedefine: {
	    IGuardMethodRedefine *ir = (IGuardMethodRedefine *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "if (!JIT_OP_UNREDEFINED_P(%d, %d)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                ir->bop, ir->klass_flag, exit_block_id);
	    break;
	}
	case OPCODE_IExit: {
	    IExit *ir = (IExit *)Inst;
	    long exit_block_id = get_sideexit_id(SideExitBBs, ir->Exit);
	    cgen_printf(gen, "goto L_exit%ld;\n", exit_block_id);
	    break;
	}
	case OPCODE_IFixnumAdd: {
	    IFixnumAdd *ir = (IFixnumAdd *)Inst;
	    EMIT_CODE(gen, IFixnumAdd, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	case OPCODE_IFixnumSub: {
	    IFixnumSub *ir = (IFixnumSub *)Inst;
	    EMIT_CODE(gen, IFixnumSub, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	case OPCODE_IFixnumMul: {
	    IFixnumMul *ir = (IFixnumMul *)Inst;
	    EMIT_CODE(gen, IFixnumMul, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	case OPCODE_IFixnumDiv: {
	    IFixnumDiv *ir = (IFixnumDiv *)Inst;
	    EMIT_CODE(gen, IFixnumDiv, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	case OPCODE_IFixnumMod: {
	    IFixnumMod *ir = (IFixnumMod *)Inst;
	    EMIT_CODE(gen, IFixnumMod, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	//case OPCODE_IFixnumPow: {
	//    IFixnumPow *ir = (IFixnumPow *)Inst;
	//    assert(0 && "need to implement");
	//    //DBL2NUM(pow(RFLOAT_VALUE(x), (double)FIX2LONG(y)));
	//    break;
	//}
	case OPCODE_IFixnumAddOverflow: {
	    IFixnumAddOverflow *ir = (IFixnumAddOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumAddOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumSubOverflow: {
	    IFixnumSubOverflow *ir = (IFixnumSubOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumSubOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumMulOverflow: {
	    IFixnumMulOverflow *ir = (IFixnumMulOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumMulOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumDivOverflow: {
	    IFixnumDivOverflow *ir = (IFixnumDivOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumModOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumModOverflow: {
	    IFixnumModOverflow *ir = (IFixnumModOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumModOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	//case OPCODE_IFixnumPowOverflow: {
	//    IFixnumPowOverflow *ir = (IFixnumPowOverflow *)Inst;
	//    assert(0 && "need to implement");
	//    //DBL2NUM(pow(RFLOAT_VALUE(x), (double)FIX2LONG(y)));
	//    break;
	//}
	case OPCODE_IFixnumEq: {
	    IFixnumEq *ir = (IFixnumEq *)Inst;
	    EMIT_CODE(gen, IFixnumEq, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumNe: {
	    IFixnumNe *ir = (IFixnumNe *)Inst;
	    EMIT_CODE(gen, IFixnumNe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumGt: {
	    IFixnumGt *ir = (IFixnumGt *)Inst;
	    EMIT_CODE(gen, IFixnumGt, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumGe: {
	    IFixnumGe *ir = (IFixnumGe *)Inst;
	    EMIT_CODE(gen, IFixnumGe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumLt: {
	    IFixnumLt *ir = (IFixnumLt *)Inst;
	    EMIT_CODE(gen, IFixnumLt, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumLe: {
	    IFixnumLe *ir = (IFixnumLe *)Inst;
	    EMIT_CODE(gen, IFixnumLe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumAnd: {
	    IFixnumAnd *ir = (IFixnumAnd *)Inst;
	    EMIT_CODE(gen, IFixnumAnd, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumOr: {
	    IFixnumOr *ir = (IFixnumOr *)Inst;
	    EMIT_CODE(gen, IFixnumOr, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumXor: {
	    IFixnumXor *ir = (IFixnumXor *)Inst;
	    EMIT_CODE(gen, IFixnumXor, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumLshift: {
	    IFixnumLshift *ir = (IFixnumLshift *)Inst;
	    EMIT_CODE(gen, IFixnumLshift, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumRshift: {
	    IFixnumRshift *ir = (IFixnumRshift *)Inst;
	    EMIT_CODE(gen, IFixnumRshift, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumNeg: {
	    IFixnumNeg *ir = (IFixnumNeg *)Inst;
	    EMIT_CODE1(gen, IFixnumNeg, Id, ir->Recv);
	    break;
	}

	case OPCODE_IFixnumComplement: {
	    IFixnumComplement *ir = (IFixnumComplement *)Inst;
	    EMIT_CODE1(gen, IFixnumComplement, Id, ir->Recv);
	    break;
	}
	case OPCODE_IFloatAdd: {
	    IFloatAdd *ir = (IFloatAdd *)Inst;
	    EMIT_CODE(gen, IFloatAdd, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatSub: {
	    IFloatSub *ir = (IFloatSub *)Inst;
	    EMIT_CODE(gen, IFloatSub, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatMul: {
	    IFloatMul *ir = (IFloatMul *)Inst;
	    EMIT_CODE(gen, IFloatMul, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatDiv: {
	    IFloatDiv *ir = (IFloatDiv *)Inst;
	    EMIT_CODE(gen, IFloatDiv, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatMod: {
	    IFloatMod *ir = (IFloatMod *)Inst;
	    EMIT_CODE(gen, IFloatMod, Id, ir->LHS, ir->RHS);
	    break;
	}
	//case OPCODE_IFloatPow: {
	//    IFloatPow *ir = (IFloatPow *)Inst;
	//    EMIT_CODE(gen, IFloatPow, Id, ir->LHS, ir->RHS);
	//    break;
	//}
	case OPCODE_IFloatNeg: {
	    IFloatNeg *ir = (IFloatNeg *)Inst;
	    EMIT_CODE1(gen, IFloatNeg, Id, ir->Recv);
	    break;
	}
	case OPCODE_IFloatEq: {
	    IFloatEq *ir = (IFloatEq *)Inst;
	    EMIT_CODE(gen, IFloatEq, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatNe: {
	    IFloatNe *ir = (IFloatNe *)Inst;
	    EMIT_CODE(gen, IFloatNe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatGt: {
	    IFloatGt *ir = (IFloatGt *)Inst;
	    EMIT_CODE(gen, IFloatGt, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatGe: {
	    IFloatGe *ir = (IFloatGe *)Inst;
	    EMIT_CODE(gen, IFloatGe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatLt: {
	    IFloatLt *ir = (IFloatLt *)Inst;
	    EMIT_CODE(gen, IFloatLt, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatLe: {
	    IFloatLe *ir = (IFloatLe *)Inst;
	    EMIT_CODE(gen, IFloatLe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IObjectNot: {
	    IObjectNot *ir = (IObjectNot *)Inst;
	    cgen_printf(gen, "v%ld = (RTEST(v%ld) == 0) ? Qfalse : Qtrue;\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	//case OPCODE_IObjectEq: {
	//    IObjectNot *ir = (IObjectNot *)Inst;
	//    cgen_printf(gen, "v%ld = (RTEST(v%ld) == 0) ? Qtrue : Qfalse;\n",
	//                Id, lir_getid(ir->Recv));
	//    break;
	//}
	//case OPCODE_IObjectNe: {
	//    IObjectNot *ir = (IObjectNot *)Inst;
	//    cgen_printf(gen, "v%ld = (RTEST(v%ld) == 0) ? Qtrue : Qfalse;\n",
	//                Id, lir_getid(ir->Recv));
	//    break;
	//}
	case OPCODE_IObjectToString: {
	    IObjectToString *ir = (IObjectToString *)Inst;
	    EMIT_CODE1(gen, IObjectToString, Id, ir->Val);
	    break;
	}
	case OPCODE_IFixnumToFloat: {
	    IFixnumToFloat *ir = (IFixnumToFloat *)Inst;
	    EMIT_CODE1(gen, IFixnumToFloat, Id, ir->Val);
	    break;
	}
	case OPCODE_IFixnumToString: {
	    IFixnumToString *ir = (IFixnumToString *)Inst;
	    EMIT_CODE1(gen, IFixnumToString, Id, ir->Val);
	    break;
	}
	case OPCODE_IFloatToFixnum: {
	    IFloatToFixnum *ir = (IFloatToFixnum *)Inst;
	    EMIT_CODE1(gen, IFloatToFixnum, Id, ir->Val);
	    break;
	}
	case OPCODE_IFloatToString: {
	    IFloatToString *ir = (IFloatToString *)Inst;
	    assert(0 && "need to implement flo_to_s");
	    EMIT_CODE1(gen, IFloatToString, Id, ir->Val);
	    break;
	}
	case OPCODE_IStringToFixnum: {
	    IStringToFixnum *ir = (IStringToFixnum *)Inst;
	    cgen_printf(gen, "v%ld = rb_str_to_inum(v%ld, 10, 0);\n", Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IStringToFloat: {
	    IStringToFloat *ir = (IStringToFloat *)Inst;
	    cgen_printf(gen, "v%ld = rb_str_to_dbl(v%ld, 0);\n", Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IMathSin: {
	    IMathSin *ir = (IMathSin *)Inst;
	    cgen_printf(gen, "v%ld = DBL2NUM(sin(RFLOAT_VALUE(v%ld)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathCos: {
	    IMathCos *ir = (IMathCos *)Inst;
	    cgen_printf(gen, "v%ld = DBL2NUM(cos(RFLOAT_VALUE(v%ld)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathTan: {
	    IMathTan *ir = (IMathTan *)Inst;
	    cgen_printf(gen, "v%ld = DBL2NUM(tan(RFLOAT_VALUE(v%ld)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathExp: {
	    IMathExp *ir = (IMathExp *)Inst;
	    cgen_printf(gen, "v%ld = DBL2NUM(exp(RFLOAT_VALUE(v%ld)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathSqrt: {
	    IMathSqrt *ir = (IMathSqrt *)Inst;
	    cgen_printf(gen, "v%ld = DBL2NUM(sqrt(RFLOAT_VALUE(v%ld)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathLog10: {
	    IMathLog10 *ir = (IMathLog10 *)Inst;
	    cgen_printf(gen, "v%ld = DBL2NUM(log10(RFLOAT_VALUE(v%ld)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathLog2: {
	    IMathLog2 *ir = (IMathLog2 *)Inst;
	    cgen_printf(gen, "v%ld = DBL2NUM(log2(RFLOAT_VALUE(v%ld)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IStringLength: {
	    IStringLength *ir = (IStringLength *)Inst;
	    cgen_printf(gen, "v%ld = rb_str_length(v%ld);\n", Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IStringEmptyP: {
	    IStringEmptyP *ir = (IStringEmptyP *)Inst;
	    cgen_printf(gen, "v%ld = (RSTRING_LEN(v%ld) == 0) ? Qtrue : Qfalse;\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IStringConcat: {
	    IStringConcat *ir = (IStringConcat *)Inst;
	    cgen_printf(gen, "v%ld = rb_str_plus(v%ld, v%ld);\n", Id,
	                lir_getid(ir->LHS),
	                lir_getid(ir->RHS));
	    break;
	}
	case OPCODE_IStringAdd: {
	    IStringAdd *ir = (IStringAdd *)Inst;
	    EMIT_CODE(gen, IStringAdd, Id, ir->Recv, ir->Obj);
	    break;
	}
	case OPCODE_IArrayLength: {
	    IArrayLength *ir = (IArrayLength *)Inst;
	    cgen_printf(gen, "v%ld = LONG2NUM(RARRAY_LEN(v%ld));\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IArrayEmptyP: {
	    IArrayEmptyP *ir = (IArrayEmptyP *)Inst;
	    cgen_printf(gen, "v%ld = (RARRAY_LEN(v%ld) == 0) ? Qtrue : Qfalse;\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IArrayConcat: {
	    IArrayConcat *ir = (IArrayConcat *)Inst;
	    cgen_printf(gen, "v%ld = rb_ary_plus(v%ld, v%ld);\n",
	                Id, lir_getid(ir->LHS),
	                lir_getid(ir->RHS));
	    break;
	}
	case OPCODE_IArrayGet: {
	    IArrayGet *ir = (IArrayGet *)Inst;
	    cgen_printf(gen, "v%ld = rb_ary_entry(v%ld, FIX2LONG(v%ld));\n", Id,
	                lir_getid(ir->Recv), lir_getid(ir->Index));
	    break;
	}
	case OPCODE_IArraySet: {
	    IArraySet *ir = (IArraySet *)Inst;
	    cgen_printf(gen, "rb_ary_store(v%ld, FIX2LONG(v%ld), v%ld);\n"
	                     "v%ld = v%ld;\n",
	                lir_getid(ir->Recv),
	                lir_getid(ir->Index),
	                lir_getid(ir->Val), Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IHashLength: {
	    IHashLength *ir = (IHashLength *)Inst;
	    cgen_printf(gen, "v%ld = INT2FIX(RHASH_SIZE(v%ld));\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IHashEmptyP: {
	    IHashEmptyP *ir = (IHashEmptyP *)Inst;
	    cgen_printf(gen,
	                "v%ld = (RHASH_EMPTY_P(v%ld) == 0) ? Qtrue : Qfalse;\n", Id,
	                lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IHashGet: {
	    IHashGet *ir = (IHashGet *)Inst;
	    cgen_printf(gen, "  v%ld = rb_hash_aref(v%ld, v%ld);\n",
	                Id, lir_getid(ir->Recv),
	                lir_getid(ir->Index));
	    break;
	}
	case OPCODE_IHashSet: {
	    IHashSet *ir = (IHashSet *)Inst;
	    cgen_printf(gen, "  rb_hash_aset(v%ld, v%ld, v%ld);\n"
	                     "  v%ld = v%ld;\n",
	                lir_getid(ir->Recv), lir_getid(ir->Index),
	                lir_getid(ir->Val), Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IRegExpMatch: {
	    IRegExpMatch *ir = (IRegExpMatch *)Inst;
	    cgen_printf(gen, "  v%ld = rb_reg_match(v%ld, v%ld);\n", Id,
	                lir_getid(ir->Re),
	                lir_getid(ir->Str));
	    break;
	}
	case OPCODE_IAllocObject: {
	    IAllocObject *ir = (IAllocObject *)Inst;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  long num = %d;\n"
	                     "  VALUE argv[%d];\n",
	                ir->argc, ir->argc + 1);
	    for (i = 0; i < ir->argc; i++) {
		cgen_printf(gen, "argv[%d] = v%ld;\n", i, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, "  v%ld = rb_class_new_instance(num, argv, v%ld);\n"
	                     "}\n",
	                Id, lir_getid(ir->Klass));
	    break;
	}

	case OPCODE_IAllocArray: {
	    IAllocArray *ir = (IAllocArray *)Inst;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  long num = %d;\n"
	                     "  VALUE argv[%d];\n",
	                ir->argc, ir->argc);
	    for (i = 0; i < ir->argc; i++) {
		cgen_printf(gen, "argv[%d] = v%ld;\n", i, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, "  v%ld = rb_ary_new4(num, argv);\n"
	                     "}\n",
	                Id);
	    break;
	}
	case OPCODE_IAllocHash: {
	    IAllocHash *ir = (IAllocHash *)Inst;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  VALUE val = rb_hash_new();\n");
	    for (i = 0; i < ir->argc; i += 2) {
		cgen_printf(gen, "rb_hash_aset(val, v%ld, v%ld);\n",
		            lir_getid(ir->argv[i]),
		            lir_getid(ir->argv[i + 1]));
	    }
	    cgen_printf(gen, "  v%ld = val;\n"
	                     "}\n",
	                Id);

	    break;
	}
	case OPCODE_IAllocString: {
	    IAllocString *ir = (IAllocString *)Inst;
	    cgen_printf(gen, "  v%ld = rb_str_resurrect(v%ld);\n",
	                Id, lir_getid(ir->OrigStr));
	    break;
	}
	case OPCODE_IAllocRange: {
	    IAllocRange *ir = (IAllocRange *)Inst;
	    cgen_printf(gen, "{\n"
	                     "  long flag = %d;\n"
	                     "  VALUE low  = v%ld;\n"
	                     "  VALUE high = v%ld;\n"
	                     "  v%ld = rb_range_new(low, high, flag);\n"
	                     "}\n",
	                ir->Flag, lir_getid(ir->Low), lir_getid(ir->High), Id);

	    break;
	}
	case OPCODE_IAllocRegexFromArray: {
	    IAllocRegexFromArray *ir = (IAllocRegexFromArray *)Inst;
	    cgen_printf(gen, "  v%ld = rb_reg_new_ary(v%ld, %d);\n",
	                Id, lir_getid(ir->Ary), ir->opt);
	    break;
	}

	case OPCODE_IGetGlobal: {
	    IGetGlobal *ir = (IGetGlobal *)Inst;
	    cgen_printf(gen, "  v%ld = GET_GLOBAL(v%ld);\n", Id, lir_getid(ir->Entry));
	    break;
	}
	case OPCODE_ISetGlobal: {
	    ISetGlobal *ir = (ISetGlobal *)Inst;
	    cgen_printf(gen, "  SET_GLOBAL(v%ld, v%ld);\n", lir_getid(ir->Entry), lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IGetPropertyName: {
	    IGetPropertyName *ir = (IGetPropertyName *)Inst;
	    cgen_printf(gen, "{\n"
	                     "  long index = %ld;\n"
	                     "  VALUE  obj = v%ld;\n"
	                     "  VALUE *ptr = ROBJECT_IVPTR(obj);\n"
	                     "  v%ld = ptr[index];\n"
	                     "}\n",
	                ir->Index, lir_getid(ir->Recv), Id);
	    break;
	}
	case OPCODE_ISetPropertyName: {
	    ISetPropertyName *ir = (ISetPropertyName *)Inst;
	    cgen_printf(gen, "{\n"
	                     "  long index = %ld;\n"
	                     "  VALUE  obj = v%ld;\n"
	                     "  VALUE  val = v%ld;\n"
	                     "  VALUE *ptr = ROBJECT_IVPTR(obj);\n"
	                     "  RB_OBJ_WRITE(obj, &ptr[index], val);\n"
	                     "  v%ld = ptr[index] = val;\n"
	                     "}\n",
	                ir->Index, lir_getid(ir->Recv), lir_getid(ir->Val), Id);
	    break;
	}
	case OPCODE_ILoadSelf: {
	    cgen_printf(gen, "v%ld = GET_SELF();\n", Id);
	    break;
	}
	case OPCODE_ILoadSelfAsBlock: {
	    ILoadSelfAsBlock *ir = (ILoadSelfAsBlock *)Inst;
	    cgen_printf(gen,
	                "{\n"
	                "  ISEQ blockiseq = (ISEQ) %p;\n"
	                "  v%ld = (VALUE) RUBY_VM_GET_BLOCK_PTR_IN_CFP(reg_cfp);\n"
	                "  /*assert(((rb_block_t *)v%ld)->iseq == NULL);*/\n"
	                "  ((rb_block_t *)v%ld)->iseq = blockiseq;\n"
	                "}\n",
	                ir->iseq, Id, Id, Id);
	    break;
	}
	case OPCODE_ILoadBlock: {
	    // ILoadBlock *ir = (ILoadBlock *) Inst;
	    cgen_printf(gen, "{\n"
	                     "  v%ld = (VALUE) VM_CF_BLOCK_PTR(reg_cfp);\n"
	                     "}\n",
	                Id);
	    break;
	}
	case OPCODE_ILoadConstNil: {
	    cgen_printf(gen, "v%ld = Qnil;\n", Id);
	    break;
	}
	case OPCODE_ILoadConstObject:
	case OPCODE_ILoadConstBoolean:
	case OPCODE_ILoadConstFixnum:
	case OPCODE_ILoadConstFloat:
	case OPCODE_ILoadConstString:
	case OPCODE_ILoadConstRegexp: {
	    ILoadConstObject *ir = (ILoadConstObject *)Inst;
	    cgen_printf(gen, "v%ld = (VALUE) 0x%lx;\n", Id, ir->Val);
	    break;
	}
	case OPCODE_IEnvStore: {
	    IEnvStore *ir = (IEnvStore *)Inst;
	    if (ir->Level > 0) {
		cgen_printf(gen, "{\n"
		                 "  int i, lev = (int)%d;\n"
		                 "  VALUE *ep = GET_EP();\n"
		                 "\n"
		                 "  for (i = 0; i < lev; i++) {\n"
		                 "      ep = GET_PREV_EP(ep);\n"
		                 "  }\n"
		                 "  *(ep - %d) = v%ld;\n"
		                 "}\n",
		            ir->Level, ir->Index, lir_getid(ir->Val));
	    }
	    else {
		cgen_printf(gen, "  *(GET_EP() - %d) = v%ld;\n", ir->Index,
		            lir_getid(ir->Val));
	    }
	    break;
	}
	case OPCODE_IEnvLoad: {
	    IEnvLoad *ir = (IEnvLoad *)Inst;
	    if (ir->Level > 0) {
		cgen_printf(gen, "{\n"
		                 "  int i, lev = (int)%d;\n"
		                 "  VALUE *ep = GET_EP();\n"
		                 "\n"
		                 "  for (i = 0; i < lev; i++) {\n"
		                 "      ep = GET_PREV_EP(ep);\n"
		                 "  }\n"
		                 "  v%ld = *(ep - %d);\n"
		                 "}\n",
		            ir->Level, Id, ir->Index);
	    }
	    else {
		cgen_printf(gen, "  v%ld = *(GET_EP() - %d);\n", Id, ir->Index);
	    }
	    break;
	}
	case OPCODE_IStackPop: {
	    cgen_printf(gen, "  v%ld = *POP();\n"
	                     "  original_sp -= 1;\n",
	                Id);
	    break;
	}

	case OPCODE_IInvokeMethod: {
	    IInvokeMethod *ir = (IInvokeMethod *)Inst;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  VALUE *basesp = GET_SP();\n"
	                     "  CALL_INFO ci = (CALL_INFO) %p;\n",
	                ir->ci);
	    for (i = 0; i < ir->argc; i++) {
		cgen_printf(gen, "(GET_SP())[%d] = v%ld;\n",
		            i, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(
	        gen,
	        "SET_SP(basesp + %d);\n"
	        "  ci->recv = v%ld;\n"
	        "  v%ld = (*(ci)->call)(th, GET_CFP(), (ci));\n"
	        "  /*assert(v%ld != Qundef && \"method must c-defined method\");*/\n"
	        "  SET_SP(basesp);\n"
	        "}\n",
	        ir->argc, lir_getid(ir->argv[0]), Id, Id);
	    break;
	}
	case OPCODE_IInvokeNative: {
	    int i;
	    IInvokeNative *ir = (IInvokeNative *)Inst;
	    cgen_printf(gen, "  v%ld = ((ruby_jit_native_func%d_t)%p)(", Id, ir->argc,
	                ir->fptr);
	    for (i = 0; i < ir->argc; i++) {
		if (i != 0) {
		    cgen_printf(gen, ", ");
		}
		cgen_printf(gen, "v%ld", lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, ");\n");
	    break;
	}
	case OPCODE_IPatternMatch: {
	    IPatternMatch *ir = (IPatternMatch *)Inst;
	    cgen_printf(
	        gen,
	        "{\n"
	        "  enum vm_check_match_type checkmatch_type =\n"
	        "       (enum vm_check_match_type) %ld\n\n"
	        "  VALUE result = Qfalse;\n"
	        "  VALUE pattern = v%ld;\n"
	        "  VALUE target  = v%ld;\n"
	        "  if (RTEST(check_match(pattern, target, checkmatch_type))) {\n"
	        "    result = Qtrue;\n"
	        "  }\n"
	        "  v%ld = result;\n"
	        "}\n",
	        ir->flag, lir_getid(ir->Pattern), lir_getid(ir->Target), Id);
	    break;
	}

	case OPCODE_IPatternMatchRange: {
	    IPatternMatchRange *ir = (IPatternMatchRange *)Inst;
	    cgen_printf(
	        gen, "{\n"
	             "  int i;\n"
	             "  enum vm_check_match_type checkmatch_type =\n"
	             "       (enum vm_check_match_type) %ld\n\n"
	             "  VALUE result = Qfalse;\n"
	             "  VALUE pattern = v%ld;\n"
	             "  VALUE target  = v%ld;\n"
	             "  for (i = 0; i < RARRAY_LEN(pattern); i++) {\n"
	             "    if (RTEST(check_match(RARRAY_AREF(pattern, i), target,\n"
	             "                          checkmatch_type))) {\n"
	             "      result = Qtrue;\n"
	             "      break;\n"
	             "    }\n"
	             "  }\n"
	             "  v%ld = result;\n"
	             "}\n",
	        ir->flag, lir_getid(ir->Pattern), lir_getid(ir->Target), Id);
	    break;
	}
	case OPCODE_IJump: {
	    IJump *ir = (IJump *)Inst;
	    cgen_printf(gen, "goto L_%u;\n", ir->TargetBB->base.id);
	    break;
	}
	case OPCODE_IJumpIf: {
	    IJumpIf *ir = (IJumpIf *)Inst;
	    assert(0 && "need test");
	    cgen_printf(gen, "if (RTEST(v%ld)) {\n"
	                     "    goto L_%u;\n"
	                     "}\n",
	                lir_getid(ir->Cond), ir->TargetBB->base.id);
	    break;
	}
	case OPCODE_IThrow: {
	    // IThrow *ir = (IThrow *) Inst;
	    assert(0 && "not implemented");
	    break;
	}
	case OPCODE_IFramePush: {
	    IFramePush *ir = (IFramePush *)Inst;
	    assert(0 && "not implemented");
	    //EmitFramePush(Rec, gen, ir, 0);
	    break;
	}
	case OPCODE_IFramePop: {
	    // IFramePop *ir = (IFramePop *) Inst;
	    cgen_printf(gen, "vm_pop_frame(th);\n"
	                     "reg_cfp = th->cfp;\n");
	    break;
	}
	// case OPCODE_IPhi : {
	//  IPhi *ir = (IPhi *) Inst;
	//  assert(0 && "not implemented");
	//  break;
	//}
	case OPCODE_ITrace: {
#if 0
        ITrace* ir = (ITrace*)Inst;
        // FIXME
        // When we enable trace code, clang saied error.
        // >> error: Must have a valid dtrace stability entry'
        // >> ld: error creating dtrace DOF section for architecture x86_64'
        // We want to enable dtrace for compatibility but we have no time to
        // implement.
        cgen_printf(gen,
                "{\n"
                "  rb_event_flag_t flag = (rb_event_flag_t)%ld;\n"
                "  if (RUBY_DTRACE_METHOD_ENTRY_ENABLED() ||\n"
                "      RUBY_DTRACE_METHOD_RETURN_ENABLED() ||\n"
                "      RUBY_DTRACE_CMETHOD_ENTRY_ENABLED() ||\n"
                "      RUBY_DTRACE_CMETHOD_RETURN_ENABLED()) {\n"
                "\n"
                "    switch(flag) {\n"
                "      case RUBY_EVENT_CALL:\n"
                "        RUBY_DTRACE_METHOD_ENTRY_HOOK(th, 0, 0);\n"
                "        break;\n"
                "      case RUBY_EVENT_C_CALL:\n"
                "        RUBY_DTRACE_CMETHOD_ENTRY_HOOK(th, 0, 0);\n"
                "        break;\n"
                "      case RUBY_EVENT_RETURN:\n"
                "        RUBY_DTRACE_METHOD_RETURN_HOOK(th, 0, 0);\n"
                "        break;\n"
                "      case RUBY_EVENT_C_RETURN:\n"
                "        RUBY_DTRACE_CMETHOD_RETURN_HOOK(th, 0, 0);\n"
                "        break;\n"
                "    }\n"
                "  }\n"
                "\n"
                "  EXEC_EVENT_HOOK(th, flag, GET_SELF(), 0,\n"
                "                  0/*id and klass are resolved at callee */,\n"
                "                  (flag & (RUBY_EVENT_RETURN |\n"
                "                  RUBY_EVENT_B_RETURN)) ? TOPN(0) : Qundef);\n"
                "}\n", ir->Flag);
#endif
	    break;
	}
	default:
	    assert(0 && "unreachable");
    }
}

static void prepare_side_exit(trace_recorder_t *rec, CGen *gen, hashmap_t *SideExitBBs)
{
    long j = 1;
    hashmap_iterator_t itr = { 0, 0 };
    hashmap_init(SideExitBBs, hashmap_size(&rec->stack_map));
    while (hashmap_next(&rec->stack_map, &itr)) {
	VALUE *pc = (VALUE *)itr.entry->key;
	hashmap_set(SideExitBBs, (hashmap_data_t)pc, (j << 1));
	cgen_printf(gen,
	            "static trace_side_exit_handler_t side_exit_handler_%ld = {\n"
	            " .exit_pc = (VALUE *) %p,\n"
	            " .this_trace = 0,\n"
	            " .child_trace = 0\n"
	            "};\n",
	            j, pc);
	j++;
    }
}

static void compile_prologue(trace_recorder_t *rec, trace_t *trace, hashmap_t *SideExitBBs, CGen *gen, int fid)
{
    hashmap_iterator_t itr = { 0, 0 };
    cgen_printf(gen,
                "#include \"ruby_jit.h\"\n"
                "#include <assert.h>\n"
                "#include <dlfcn.h>\n"
                "#define BLOCK_LABEL(label) L_##label:;(void)&&L_##label;\n"
                "static const jit_runtime_t *local_jit_runtime = NULL;\n");

    if (JIT_DUMP_COMPILE_LOG > 0) {
	const rb_iseq_t *iseq = trace->iseq;
	VALUE file = iseq->location.path;
	cgen_printf(gen,
	            "// This code is translated from file=%s line=%d\n",
	            RSTRING_PTR(file),
	            rb_iseq_line_no(iseq,
	                            trace->start_pc - iseq->iseq_encoded));
    }

    prepare_side_exit(rec, gen, SideExitBBs);

    cgen_printf(gen,
                "void init_ruby_jit_%d(const jit_runtime_t *ctx, struct jit_trace *t)"
                "{\n"
                "  local_jit_runtime = ctx;\n"
                "  (void) make_no_method_exception;\n"
                //"  (void) jit_vm_call_iseq_setup_normal;\n"
                //"  (void) jit_vm_yield_setup_block_args;\n"
                ,
                fid);

    while (hashmap_next(SideExitBBs, &itr)) {
	long id = ((long)itr.entry->val) >> 1;
	cgen_printf(gen, "  side_exit_handler_%ld.this_trace = t;\n", id);
    }
    cgen_printf(gen, "}\n");

    cgen_printf(gen, "trace_side_exit_handler_t *ruby_jit_%d(rb_thread_t *th,\n"
                     "    rb_control_frame_t *reg_cfp)\n"
                     "{\n"
                     "  VALUE *original_sp = GET_SP();\n"
                     "  rb_control_frame_t *original_cfp = reg_cfp;\n",
                fid);
}

static void compile_epilogue(trace_t *trace, CGen *gen)
{
}

static void compile_sideexit(trace_recorder_t *rec, trace_t *trace, CGen *gen, hashmap_t *SideExitBBs)
{
    /*
       sp[0..n] = StackMap[0..n]
     * return PC;
     */
    int i, j;
    hashmap_iterator_t itr = { 0, 0 };
    while (hashmap_next(SideExitBBs, &itr)) {
	VALUE *pc = (VALUE *)itr.entry->key;
	long block_id = itr.entry->val >> 1;
	regstack_t *stack = (regstack_t *)hashmap_get(&rec->stack_map, (hashmap_data_t)pc);
	cgen_printf(gen, "L_exit%ld:;\n", block_id);
	cgen_printf(gen, "//fprintf(stderr,\"exit%ld : pc=%p\\n\");\n", block_id, pc);

	j = 0;
	cgen_printf(gen, "th->cfp = reg_cfp = original_cfp;\n");
	cgen_printf(gen, "SET_SP(original_sp);\n");
	for (i = 0; i < stack->list.size; i++) {
	    lir_t inst = (lir_t)stack->list.list[i];
	    if (inst) {
		if (lir_opcode(inst) == OPCODE_IFramePush) {
		    IFramePush *ir = (IFramePush *)inst;
		    cgen_printf(gen, "SET_SP(GET_SP() + %d);\n", j);
		    assert(0 && "FIXME");
		    //EmitFramePush(Rec, gen, ir, 1);
		    j = 0;
		}
		else {
		    lir_t ir = (lir_t)stack->list.list[i];
		    cgen_printf(gen, "(GET_SP())[%d] = v%ld;\n", j, lir_getid(ir));
		    j++;
		}
	    }
	}

	cgen_printf(gen, "SET_SP(GET_SP() + %d);\n"
	                 "return &side_exit_handler_%ld;\n",
	            j, block_id);
    }
}

static void compile2c(trace_recorder_t *rec, CGen *gen, trace_t *trace, int fid)
{
    unsigned i, j;
    hashmap_t SideExitBBs;
    compile_prologue(rec, trace, &SideExitBBs, gen, fid);
    for (i = 0; i < rec->bblist.size; i++) {
	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, i);
	for (j = 0; j < bb->insts.size; j++) {
	    lir_inst_t *inst = (lir_inst_t *)bb->insts.list[j];
	    if (lir_inst_define_value(lir_opcode(inst))) {
		long Id = lir_getid(inst);
		cgen_printf(gen, "VALUE v%ld = 0;\n", Id);
	    }
	}
    }

    for (i = 0; i < rec->bblist.size; i++) {
	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, i);
	cgen_printf(gen, "BLOCK_LABEL(%u);\n", bb->base.id);
	for (j = 0; j < bb->insts.size; j++) {
	    lir_inst_t *inst = (lir_inst_t *)bb->insts.list[j];
	    compile_inst(rec, gen, &SideExitBBs, inst);
	}
    }

    compile_sideexit(rec, trace, gen, &SideExitBBs);
    cgen_printf(gen, "}\n");
    compile_epilogue(trace, gen);
    hashmap_dispose(&SideExitBBs, 0);
}

static void trace_compile(trace_recorder_t *rec, trace_t *trace)
{
    static int serial_id = 0;
    char path[128] = {};
    char fname[128] = {};
    CGen gen;
    int id = serial_id++;

    assert(serial_id < JIT_MAX_COMPILE_TRACE && "too many compiled trace");
    if (id == 0) {
	//gwlocal_jit_runtime_init();
    }

    snprintf(fname, 128, "ruby_jit_%d", id);
    snprintf(path, 128, "/tmp/ruby_jit.%d.%d.dylib", (unsigned)getpid(), id);

    cgen_open(&gen, FILE_MODE, path, id);

    compile2c(rec, &gen, trace, id);
    if (cgen_freeze(&gen, id) != 0) {
	//trace->Code = (void *) 0xdeadbeaf;
    }
    else {
	trace_recorder_freeze_cache(rec);
	cgen_get_function(&gen, fname, trace);
    }
    cgen_close(&gen);
}
