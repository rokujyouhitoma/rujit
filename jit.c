/**********************************************************************

  jit.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/vm.h"
#include "gc.h"
#include "vm_core.h"
#include "internal.h"
#include "insns.inc"
#include "insns_info.inc"
#include "vm_insnhelper.h"
#include "vm_exec.h"
#include "iseq.h"

#include "jit.h"
#include "jit_opts.h"
#include "jit_prelude.c"
#include "jit_hashmap.c"

// imported api from ruby-core
extern void vm_search_method(rb_call_info_t *ci, VALUE recv);
extern VALUE rb_f_block_given_p(void);

static inline int
check_cfunc(const rb_method_entry_t *me, VALUE (*func)())
{
    return me && me->def->type == VM_METHOD_TYPE_CFUNC && me->def->body.cfunc.func == func;
}

/* original code is copied from vm_insnhelper.c vm_getivar() */
static int vm_load_cache(VALUE obj, ID id, IC ic, rb_call_info_t *ci, int is_attr)
{
    if (RB_TYPE_P(obj, T_OBJECT)) {
	VALUE klass = RBASIC(obj)->klass;
	st_data_t index;
	long len = ROBJECT_NUMIV(obj);
	struct st_table *iv_index_tbl = ROBJECT_IV_INDEX_TBL(obj);

	if (iv_index_tbl) {
	    if (st_lookup(iv_index_tbl, id, &index)) {
		if ((long)index < len) {
		    //VALUE val = Qundef;
		    //VALUE *ptr = ROBJECT_IVPTR(obj);
		    //val = ptr[index];
		}
		if (!is_attr) {
		    ic->ic_value.index = index;
		    ic->ic_serial = RCLASS_SERIAL(klass);
		}
		else { /* call_info */
		    ci->aux.index = index + 1;
		}
		return 1;
	    }
	}
    }
    return 0;
}

// rujit
static int disable_jit = 0;
jit_runtime_t jit_runtime = {};

typedef struct rb_jit_t rb_jit_t;

typedef struct jit_trace trace_t;

struct memory_pool {
    struct page_chunk *head;
    struct page_chunk *root;
    unsigned pos;
    unsigned size;
};

typedef struct jit_list {
    uintptr_t *list;
    unsigned size;
    unsigned capacity;
} jit_list_t;

typedef struct const_pool {
    jit_list_t list;
    jit_list_t inst;
} const_pool_t;

typedef struct regstack {
    jit_list_t list;
} regstack_t;

typedef struct call_stack {
    jit_list_t list;
} call_stack_t;

typedef struct lir_inst_t {
    unsigned id;
    unsigned short flag;
    unsigned short opcode;
    struct lir_basicblock_t *parent;
    jit_list_t *user;
} lir_inst_t, *lir_t;

typedef struct lir_basicblock_t {
    lir_inst_t base;
    VALUE *start_pc;
    call_stack_t *call_stack;
    jit_list_t insts;
    jit_list_t succs;
    jit_list_t preds;
} basicblock_t;

typedef struct trace_side_exit_handler trace_side_exit_handler_t;

struct jit_trace {
    void *code;
    VALUE *start_pc;
    VALUE *last_pc;
    trace_side_exit_handler_t *parent;
    // for debug usage
    const rb_iseq_t *iseq;
    long counter;
    void *handler;
    const_pool_t cpool;
};

struct trace_side_exit_handler {
    trace_t *this_trace;
    VALUE *exit_pc;
};

#define TRACE_ERROR_INFO(OP, TAIL)                                        \
    OP(OK, "ok")                                                          \
    OP(NATIVE_METHOD, "invoking native method")                           \
    OP(THROW, "throw exception")                                          \
    OP(UNSUPPORT_OP, "not supported bytecode")                            \
    OP(LEAVE, "this trace return into native method")                     \
    OP(REGSTACK_UNDERFLOW, "register stack underflow")                    \
    OP(ALREADY_RECORDED, "this instruction is already recorded on trace") \
    OP(BUFFER_FULL, "trace buffer is full")                               \
    TAIL

#define DEFINE_TRACE_ERROR_STATE(NAME, MSG) TRACE_ERROR_##NAME,
#define DEFINE_TRACE_ERROR_MESSAGE(NAME, MSG) MSG,
enum trace_error_state {
    TRACE_ERROR_INFO(DEFINE_TRACE_ERROR_STATE, TRACE_ERROR_END = -1)
};

static const char *trace_error_message[] = {
    TRACE_ERROR_INFO(DEFINE_TRACE_ERROR_MESSAGE, "")
};

typedef struct jit_event_t {
    rb_thread_t *th;
    rb_control_frame_t *cfp;
    VALUE *pc;
    trace_t *trace;
    int opcode;
    enum trace_error_state reason;
} jit_event_t;

typedef struct trace_recorder {
    jit_event_t *current_event;
    trace_t *trace;
    basicblock_t *cur_bb;
    basicblock_t *entry_bb;
    struct lir_inst_t **insts;
    unsigned flag;
    unsigned last_inst_id;
    struct memory_pool mpool;
    jit_list_t bblist;
    regstack_t regstack;
    hashmap_t stack_map;
} trace_recorder_t;

typedef enum trace_mode {
    TraceModeDefault = 0,
    TraceModeRecording = 1,
    TraceModeError = -1
} trace_mode_t;

struct rb_jit_t {
    VALUE self;
    trace_t *current_trace;
    trace_recorder_t *recorder;
    trace_mode_t mode;
    hashmap_t traces;
};

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
    if (DUMP_INST > 0) {
	long pc = (e->pc - e->cfp->iseq->iseq_encoded);
	fprintf(stderr, "%04ld pc=%p %02d %s\n",
	        pc, e->pc, e->opcode, insn_name(e->opcode));
    }
}

static jit_event_t *jit_init_event(jit_event_t *e, rb_jit_t *jit, rb_thread_t *th, rb_control_frame_t *cfp, VALUE *pc)
{
    e->th = th;
    e->cfp = cfp;
    e->pc = pc;
    e->trace = jit->current_trace;
    e->opcode = get_opcode(e->cfp, e->pc);
    return e;
}

static lir_t trace_recorder_insert_stackpop(trace_recorder_t *rec);
static void dump_trace(trace_recorder_t *rec);
static void stop_recording(rb_jit_t *jit);
static call_stack_t *call_stack_new(struct memory_pool *mp);
static call_stack_t *call_stack_clone(struct memory_pool *mp, call_stack_t *old);
static lir_t Emit_StackPop(trace_recorder_t *rec);
static lir_t Emit_Jump(trace_recorder_t *rec, basicblock_t *bb);
static lir_t Emit_Exit(trace_recorder_t *rec, VALUE *target_pc);
static int lir_is_terminator(lir_inst_t *inst);
static int lir_is_guard(lir_inst_t *inst);
static int elimnate_guard(trace_recorder_t *rec, lir_inst_t *inst);

static rb_jit_t *current_jit = NULL;
static VALUE rb_cMath;

/* redefined_flag { */
static short jit_vm_redefined_flag[JIT_BOP_EXT_LAST_];
static st_table *jit_opt_method_table = 0;

#define JIT_OP_UNREDEFINED_P(op, klass) (LIKELY((jit_vm_redefined_flag[(op)] & (klass)) == 0))

#define MATH_REDEFINED_OP_FLAG (1 << 9)
static int
jit_redefinition_check_flag(VALUE klass)
{
    if (klass == rb_cFixnum)
	return FIXNUM_REDEFINED_OP_FLAG;
    if (klass == rb_cFloat)
	return FLOAT_REDEFINED_OP_FLAG;
    if (klass == rb_cString)
	return STRING_REDEFINED_OP_FLAG;
    if (klass == rb_cArray)
	return ARRAY_REDEFINED_OP_FLAG;
    if (klass == rb_cHash)
	return HASH_REDEFINED_OP_FLAG;
    if (klass == rb_cBignum)
	return BIGNUM_REDEFINED_OP_FLAG;
    if (klass == rb_cSymbol)
	return SYMBOL_REDEFINED_OP_FLAG;
    if (klass == rb_cTime)
	return TIME_REDEFINED_OP_FLAG;
    if (klass == rb_cRegexp)
	return REGEXP_REDEFINED_OP_FLAG;
    if (klass == rb_cMath)
	return MATH_REDEFINED_OP_FLAG;
    return 0;
}

void rb_jit_check_redefinition_opt_method(const rb_method_entry_t *me, VALUE klass)
{
    if (!disable_jit) {
	return;
    }
    st_data_t bop;
    int flag = jit_redefinition_check_flag(klass);
    if (st_lookup(jit_opt_method_table, (st_data_t)me, &bop)) {
	assert(flag != 0);
	jit_vm_redefined_flag[bop] |= flag;
    }
    if ((jit_vm_redefined_flag[bop] & GET_VM()->redefined_flag[bop]) == GET_VM()->redefined_flag[bop]) {
	jit_vm_redefined_flag[bop] |= GET_VM()->redefined_flag[bop];
    }
}

/* copied from vm.c */
static void add_opt_method(VALUE klass, ID mid, VALUE bop)
{
    rb_method_entry_t *me = rb_method_entry_at(klass, mid);

    if (me && me->def && me->def->type == VM_METHOD_TYPE_CFUNC) {
	st_insert(jit_opt_method_table, (st_data_t)me, (st_data_t)bop);
    }
    else {
	rb_bug("undefined optimized method: %s", rb_id2name(mid));
    }
}

static void rb_jit_init_redefined_flag(void)
{
#define DEF(k, mid, bop)                              \
    do {                                              \
	jit_vm_redefined_flag[bop] = 0;               \
	add_opt_method(rb_c##k, rb_intern(mid), bop); \
    } while (0)
    jit_opt_method_table = st_init_numtable();
    DEF(Fixnum, "&", JIT_BOP_AND);
    DEF(Fixnum, "|", JIT_BOP_OR);
    DEF(Fixnum, "^", JIT_BOP_XOR);
    DEF(Fixnum, ">>", JIT_BOP_RSHIFT);
    DEF(Fixnum, "~", JIT_BOP_INV);
    DEF(Fixnum, "**", JIT_BOP_POW);
    DEF(Float, "**", JIT_BOP_POW);

    DEF(Fixnum, "-@", JIT_BOP_NEG);
    DEF(Float, "-@", JIT_BOP_NEG);
    DEF(Fixnum, "to_f", JIT_BOP_TO_F);
    DEF(Float, "to_f", JIT_BOP_TO_F);
    DEF(String, "to_f", JIT_BOP_TO_F);

    DEF(Float, "to_i", JIT_BOP_TO_I);
    DEF(String, "to_i", JIT_BOP_TO_I);

    DEF(Fixnum, "to_s", JIT_BOP_TO_S);
    DEF(Float, "to_s", JIT_BOP_TO_S);
    DEF(String, "to_s", JIT_BOP_TO_S);

    DEF(Math, "sin", JIT_BOP_SIN);
    DEF(Math, "cos", JIT_BOP_COS);
    DEF(Math, "tan", JIT_BOP_TAN);
    DEF(Math, "exp", JIT_BOP_EXP);
    DEF(Math, "sqrt", JIT_BOP_SQRT);
    DEF(Math, "log10", JIT_BOP_LOG10);
    DEF(Math, "log2", JIT_BOP_LOG2);
#undef DEF
}
/* } redefined_flag */

/* memory pool { */
#define SIZEOF_MEMORY_POOL (4096 - sizeof(void *))
struct page_chunk {
    struct page_chunk *next;
    char buf[SIZEOF_MEMORY_POOL];
};

static void memory_pool_init(struct memory_pool *mp)
{
    mp->pos = 0;
    mp->size = 0;
    mp->head = mp->root = NULL;
}

static void memory_pool_reset(struct memory_pool *mp, int alloc_memory)
{
    struct page_chunk *root;
    struct page_chunk *page;
    root = mp->root;
    mp->size = 0;
    if (root) {
	page = root->next;
	while (page != NULL) {
	    struct page_chunk *next = page->next;
	    free(page);
	    page = next;
	}
	if (!alloc_memory) {
	    free(root);
	    root = NULL;
	}
	mp->pos = 0;
	mp->size = SIZEOF_MEMORY_POOL;
    }
    mp->head = mp->root = root;
}

static void *memory_pool_alloc(struct memory_pool *mp, size_t size)
{
    void *ptr;
    if (mp->pos + size > mp->size) {
	struct page_chunk *page;
	page = (struct page_chunk *)malloc(sizeof(struct page_chunk));
	page->next = NULL;
	if (mp->head) {
	    mp->head->next = page;
	}
	mp->head = page;
	mp->pos = 0;
	mp->size = SIZEOF_MEMORY_POOL;
    }
    ptr = mp->head->buf + mp->pos;
    memset(ptr, 0, size);
    mp->pos += size;
    return ptr;
}
/* } memory pool */

/* jit_list { */

static void jit_list_init(jit_list_t *self)
{
    self->list = NULL;
    self->size = self->capacity = 0;
}

static uintptr_t jit_list_get(jit_list_t *self, int idx)
{
    assert(0 <= idx && idx < self->size);
    return self->list[idx];
}

static void jit_list_set(jit_list_t *self, int idx, uintptr_t val)
{
    assert(0 <= idx && idx < self->size);
    self->list[idx] = val;
}

static int jit_list_indexof(jit_list_t *self, uintptr_t val)
{
    unsigned i;
    for (i = 0; i < self->size; i++) {
	if (self->list[i] == (uintptr_t)val) {
	    return i;
	}
    }
    return -1;
}

static void jit_list_ensure(jit_list_t *self, unsigned capacity)
{
    if (capacity > self->capacity) {
	if (self->capacity == 0) {
	    self->capacity = capacity;
	    self->list = (uintptr_t *)malloc(sizeof(uintptr_t) * self->capacity);
	}
	else {
	    while (capacity > self->capacity) {
		self->capacity *= 2;
	    }
	    self->list = (uintptr_t *)realloc(self->list, sizeof(uintptr_t) * self->capacity);
	}
    }
}

static void jit_list_add(jit_list_t *self, uintptr_t val)
{
    jit_list_ensure(self, self->size + 1);
    self->list[self->size++] = val;
}

static void jit_list_remove(jit_list_t *self, uintptr_t val)
{
    int i;
    if ((i = jit_list_indexof(self, val)) != -1) {
	if (i != self->size) {
	    self->size -= 1;
	    memmove(self->list + i, self->list + i + 1,
	            sizeof(uintptr_t) * (self->size - i));
	}
    }
}

static void jit_list_delete(jit_list_t *self)
{
    if (self->capacity) {
	free(self->list);
	self->list = NULL;
	self->size = self->capacity = 0;
    }
}

/* } jit_list */

/* const pool { */

#define CONST_POOL_INIT_SIZE 1
static const_pool_t *const_pool_init(const_pool_t *self)
{
    jit_list_init(&self->list);
    jit_list_init(&self->inst);
    return self;
}

static int const_pool_contain(const_pool_t *self, const void *ptr)
{
    return jit_list_indexof(&self->list, (uintptr_t)ptr);
    unsigned i;
    for (i = 0; i < self->list.size; i++) {
	if (self->list.list[i] == (uintptr_t)ptr) {
	    return i;
	}
    }
    return -1;
}

static int const_pool_add(const_pool_t *self, const void *ptr, lir_t val)
{
    unsigned idx;
    if ((idx = const_pool_contain(self, ptr)) != -1) {
	return idx;
    }
    jit_list_add(&self->list, (uintptr_t)ptr);
    jit_list_add(&self->inst, (uintptr_t)val);
    return self->list.size - 1;
}

static void const_pool_delete(const_pool_t *self)
{
    jit_list_delete(&self->list);
    jit_list_delete(&self->inst);
}

/* } const pool */

/* lir_inst {*/
static inline uintptr_t lir_getid(lir_t ir)
{
    return ir->id;
}

static void *lir_inst_init(void *ptr, unsigned opcode)
{
    lir_inst_t *inst = (lir_inst_t *)ptr;
    inst->id = 0;
    inst->flag = 0;
    inst->opcode = opcode;
    inst->parent = NULL;
    inst->user = NULL;
    return inst;
}

static lir_inst_t *lir_inst_allocate(trace_recorder_t *recorder, lir_inst_t *src, unsigned inst_size)
{
    lir_inst_t *dst = memory_pool_alloc(&recorder->mpool, inst_size);
    memcpy(dst, src, inst_size);
    dst->id = recorder->last_inst_id++;
    return dst;
}

/* } lir_inst */

/* basicblock { */
static basicblock_t *basicblock_new(struct memory_pool *mp, VALUE *start_pc)
{
    basicblock_t *bb = (basicblock_t *)memory_pool_alloc(mp, sizeof(*bb));
    bb->call_stack = NULL;
    bb->start_pc = start_pc;
    jit_list_init(&bb->insts);
    jit_list_init(&bb->succs);
    jit_list_init(&bb->preds);
    return bb;
}

static void basicblock_delete(basicblock_t *bb)
{
    jit_list_delete(&bb->insts);
    jit_list_delete(&bb->succs);
    jit_list_delete(&bb->preds);
}

static unsigned basicblock_size(basicblock_t *bb)
{
    return bb->insts.size;
}

static void basicblock_replace_inst(basicblock_t *bb, lir_t oldinst, lir_t newinst)
{
    int idx = jit_list_indexof(&bb->insts, (uintptr_t)oldinst);
    if (idx >= 0) {
	jit_list_set(&bb->insts, idx, (uintptr_t)newinst);
    }
}

static void basicblock_swap_inst(basicblock_t *bb, int idx1, int idx2)
{
    lir_t inst1 = (lir_t)jit_list_get(&bb->insts, idx1);
    lir_t inst2 = (lir_t)jit_list_get(&bb->insts, idx2);
    jit_list_set(&bb->insts, idx1, (uintptr_t)inst2);
    jit_list_set(&bb->insts, idx2, (uintptr_t)inst1);
}

static void basicblock_append(basicblock_t *bb, lir_inst_t *inst)
{
    jit_list_add(&bb->insts, (uintptr_t)inst);
}

static lir_t basicblock_get_terminator(basicblock_t *bb)
{
    lir_t reg;
    if (bb->insts.size == 0) {
	return NULL;
    }
    reg = (lir_t)jit_list_get(&bb->insts, bb->insts.size - 1);
    if (lir_is_terminator(reg)) {
	return reg;
    }
    return NULL;
}

static void basicblock_set_callstack(struct memory_pool *mpool, basicblock_t *bb, basicblock_t *old)
{
    if (old == NULL || old->call_stack) {
	bb->call_stack = call_stack_new(mpool);
    }
    else {
	bb->call_stack = call_stack_clone(mpool, old->call_stack);
    }
}

static int basicblock_call_stack_size(basicblock_t *bb)
{
    if (bb->call_stack == NULL) {
	return 0;
    }
    return bb->call_stack->list.size;
}

static lir_t basicblock_get(basicblock_t *bb, int i)
{
    return (lir_t)jit_list_get(&bb->insts, i);
}

/* } basicblock */

/* regstack { */
static regstack_t *regstack_init(regstack_t *stack)
{
    int i;
    jit_list_init(&stack->list);
    for (i = 0; i < LIR_RESERVED_REGSTACK_SIZE; i++) {
	jit_list_add(&stack->list, 0);
    }
    return stack;
}

static regstack_t *regstack_new(struct memory_pool *mpool)
{
    regstack_t *stack = (regstack_t *)memory_pool_alloc(mpool, sizeof(*stack));
    return regstack_init(stack);
}

static void regstack_push(trace_recorder_t *rec, regstack_t *stack, lir_t reg)
{
    assert(reg != NULL);
    jit_list_add(&stack->list, (uintptr_t)reg);
    if (DUMP_STACK_MAP) {
	fprintf(stderr, "push: %d %p\n", stack->list.size, reg);
    }
}

static lir_t regstack_pop(trace_recorder_t *rec, regstack_t *stack)
{
    lir_t reg;
    if (stack->list.size == 0) {
	assert(0 && "FIXME stack underflow");
    }
    reg = (lir_t)jit_list_get(&stack->list, stack->list.size - 1);
    stack->list.size--;
    if (reg == NULL) {
	reg = trace_recorder_insert_stackpop(rec);
    }
    if (DUMP_STACK_MAP) {
	fprintf(stderr, "pop: %d %p\n", stack->list.size, reg);
    }
    return reg;
}

static regstack_t *regstack_clone(struct memory_pool *mpool, regstack_t *old)
{
    unsigned i;
    regstack_t *stack = regstack_new(mpool);
    jit_list_ensure(&stack->list, old->list.size);
    for (i = 0; i < old->list.size; i++) {
	jit_list_add(&stack->list, jit_list_get(&old->list, i));
    }
    return stack;
}

static lir_t regstack_get_direct(regstack_t *stack, int n)
{
    assert(0 <= n && n < stack->list.size);
    return (lir_t)jit_list_get(&stack->list, n);
}

static void regstack_set_direct(regstack_t *stack, int n, lir_t val)
{
    assert(0 <= n && n < stack->list.size);
    jit_list_set(&stack->list, n, (uintptr_t)val);
}

static void regstack_set(regstack_t *stack, int n, lir_t reg)
{
    n = stack->list.size - n - 1;
    if (DUMP_STACK_MAP) {
	fprintf(stderr, "set: %d %p\n", n, reg);
    }
    assert(reg != NULL);
    regstack_set_direct(stack, n, reg);
}

static lir_t regstack_top(regstack_t *stack, int n)
{
    lir_t reg;
    int i, idx = stack->list.size - n - 1;
    assert(0 <= n && n < stack->list.size);
    reg = (lir_t)jit_list_get(&stack->list, n);
    if (reg == NULL && n < LIR_RESERVED_REGSTACK_SIZE) {
	assert(0 && "FIXME stack underflow");
    }
    assert(reg != NULL);
    if (DUMP_STACK_MAP) {
	fprintf(stderr, "top: %d %p\n", n, reg);
    }
    return reg;
}

static void regstack_delete(regstack_t *stack)
{
    jit_list_delete(&stack->list);
}
/* } regstack */

/* variable_table { */
typedef struct variable_table {
    jit_list_t table;
    int level;
} variable_table_t;

static variable_table_t *variable_table_init(struct memory_pool *mp, int level)
{
    variable_table_t *vtable;
    vtable = (variable_table_t *)memory_pool_alloc(mp, sizeof(*vtable));
    vtable->level = level;
    return vtable;
}
static void variable_table_expand(variable_table_t *vtable, unsigned idx)
{
    assert(idx >= 0);
    while (vtable->table.size <= idx) {
	jit_list_add(&vtable->table, 0);
    }
}

static void variable_table_set(variable_table_t *vtable, unsigned idx, lir_t reg)
{
    variable_table_expand(vtable, idx);
    jit_list_set(&vtable->table, idx, (uintptr_t)reg);
}

static lir_t variable_table_get(variable_table_t *vtable, unsigned idx)
{
    if (idx >= vtable->table.size) {
	return NULL;
    }
    return (lir_t)jit_list_get(&vtable->table, idx);
}

static int variable_table_equal(variable_table_t *vt1, variable_table_t *vt2)
{
    unsigned i;
    if (vt1->table.size != vt2->table.size) {
	return 0;
    }
    for (i = 0; i < vt1->table.size; i++) {
	lir_t r1 = variable_table_get(vt1, i);
	lir_t r2 = variable_table_get(vt2, i);
	if (r1 != r2) {
	    return 0;
	}
    }
    return 1;
}

static void variable_table_delete(variable_table_t *vtable)
{
    jit_list_delete(&vtable->table);
}
/* } variable_table */

/* call_stack { */
static call_stack_t *call_stack_new(struct memory_pool *mp)
{
    call_stack_t *cs = (call_stack_t *)memory_pool_alloc(mp, sizeof(*cs));
    jit_list_init(&cs->list);
    return cs;
}

static void call_stack_push(call_stack_t *cs, variable_table_t *vtable)
{
    jit_list_add(&cs->list, (uintptr_t)vtable);
}

static variable_table_t *call_stack_get(call_stack_t *cs, unsigned idx)
{
    if (idx >= cs->list.size) {
	return NULL;
    }
    return (variable_table_t *)jit_list_get(&cs->list, idx);
}

static variable_table_t *call_stack_pop(call_stack_t *cs)
{
    variable_table_t *vtable = call_stack_get(cs, cs->list.size - 1);
    cs->list.size -= 1;
    return vtable;
}

static int call_stack_equal(call_stack_t *cs1, call_stack_t *cs2)
{
    unsigned i;
    if (cs1->list.size != cs2->list.size) {
	return 0;
    }
    for (i = 0; i < cs1->list.size; i++) {
	variable_table_t *vtable1, *vtable2;
	vtable1 = call_stack_get(cs1, i);
	vtable2 = call_stack_get(cs2, i);
	if (variable_table_equal(vtable1, vtable2)) {
	    return 0;
	}
    }
    return 1;
}

static call_stack_t *call_stack_clone(struct memory_pool *mp, call_stack_t *old)
{
    unsigned i;
    call_stack_t *cs = call_stack_new(mp);
    for (i = 0; i < old->list.size; i++) {
	call_stack_push(cs, call_stack_get(old, i));
    }
    return cs;
}

static void call_stack_delete(call_stack_t *cs)
{
    unsigned i;
    for (i = 0; i < cs->list.size; i++) {
	variable_table_delete(call_stack_get(cs, i));
    }
    jit_list_delete(&cs->list);
}
/* } call_stack */

/* trace_recorder { */
static basicblock_t *trace_recorder_create_block(trace_recorder_t *rec, VALUE *pc);

static trace_recorder_t *trace_recorder_new()
{
    trace_recorder_t *recorder = (trace_recorder_t *)malloc(sizeof(*recorder));
    memset(recorder, 0, sizeof(*recorder));
    memory_pool_init(&recorder->mpool);
    jit_list_init(&recorder->bblist);
    regstack_init(&recorder->regstack);
    hashmap_init(&recorder->stack_map, 1);
    return recorder;
}

static void trace_recorder_delete(trace_recorder_t *recorder)
{
    memory_pool_init(&recorder->mpool);
    jit_list_delete(&recorder->bblist);
    regstack_delete(&recorder->regstack);
    hashmap_dispose(&recorder->stack_map, (hashmap_entry_destructor_t)regstack_delete);
    free(recorder);
}

static void trace_recorder_clear(trace_recorder_t *rec, trace_t *trace, int alloc_memory)
{
    rec->last_inst_id = 0;
    rec->bblist.size = 0;
    rec->trace = trace;
    regstack_delete(&rec->regstack);
    regstack_init(&rec->regstack);
    memory_pool_reset(&rec->mpool, alloc_memory);
    rec->entry_bb = rec->cur_bb = trace_recorder_create_block(rec, NULL);
    hashmap_dispose(&rec->stack_map, (hashmap_entry_destructor_t)regstack_delete);
    hashmap_init(&rec->stack_map, 1);
}

static int trace_recorder_is_full(trace_recorder_t *recorder)
{
    /*reserve one instruction for EXIT */
    return recorder->last_inst_id - 2 == LIR_MAX_TRACE_LENGTH;
}

static lir_t trace_recorder_get_const(trace_recorder_t *recorder, VALUE value)
{
    int idx = const_pool_contain(&recorder->trace->cpool, (const void *)value);
    if (idx >= 0) {
	return (lir_t)jit_list_get(&recorder->trace->cpool.inst, idx);
    }
    return NULL;
}

static lir_t trace_recorder_add_const(trace_recorder_t *recorder, VALUE value, lir_t reg)
{
    int idx = const_pool_add(&recorder->trace->cpool, (const void *)value, reg);
    return (lir_t)jit_list_get(&recorder->trace->cpool.inst, idx);
}

static CALL_INFO trace_recorder_clone_cache(trace_recorder_t *recorder, CALL_INFO ci)
{
    assert(0 && "FIXME implement");
    return 0;
}

static void trace_recorder_take_snapshot(trace_recorder_t *recorder, VALUE *pc)
{
    regstack_t *stack;
    if ((stack = (regstack_t *)hashmap_get(&recorder->stack_map, (hashmap_data_t)pc))) {
	fprintf(stderr, "rewrite snapshot\n");
	regstack_delete(stack);
    }
    stack = regstack_clone(&recorder->mpool, &recorder->regstack);
    hashmap_set(&recorder->stack_map, (hashmap_data_t)pc, (hashmap_data_t)stack);
}

static basicblock_t *trace_recorder_get_block(trace_recorder_t *rec, VALUE *pc)
{
    unsigned i;
    for (i = 0; i < rec->bblist.size; i++) {
	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, i);
	if (bb->start_pc == pc) {
	    return bb;
	}
    }
    return NULL;
}

static void compile_trace(rb_jit_t *jit, trace_recorder_t *recorder)
{
    fprintf(stderr, "compiled\n");
    dump_trace(recorder);
    asm volatile("int3");
}

static void trace_recorder_abort(trace_recorder_t *rec, jit_event_t *e, enum trace_error_state reason)
{
    rb_jit_t *jit = current_jit;
    if (reason != TRACE_ERROR_OK) {
	rb_control_frame_t *reg_cfp = e->cfp;
	const rb_iseq_t *iseq = GET_ISEQ();
	VALUE file = iseq->location.path;
	fprintf(stderr, "failed to trace at file:%s line:%d because %s\n",
	        RSTRING_PTR(file),
	        rb_iseq_line_no(iseq, e->pc - iseq->iseq_encoded),
	        trace_error_message[reason]);
	trace_recorder_take_snapshot(rec, e->pc);
	Emit_Exit(rec, e->pc);
	if (reason != TRACE_ERROR_REGSTACK_UNDERFLOW) {
	    compile_trace(jit, rec);
	}
    }
    stop_recording(jit);
    rec->trace = NULL;
    trace_recorder_clear(rec, NULL, 0);
}

static basicblock_t *trace_recorder_create_block(trace_recorder_t *rec, VALUE *pc)
{
    basicblock_t *bb = basicblock_new(&rec->mpool, pc);
    bb->base.id = rec->bblist.size;
    basicblock_set_callstack(&rec->mpool, bb, rec->cur_bb);
    jit_list_add(&rec->bblist, (uintptr_t)bb);
    return bb;
}

static lir_t trace_recorder_insert_stackpop(trace_recorder_t *rec)
{
    basicblock_t *entry_bb = rec->entry_bb;
    basicblock_t *cur_bb = rec->cur_bb;
    lir_t reg;
    int need_swap = basicblock_get_terminator(entry_bb) != NULL;
    rec->cur_bb = entry_bb;
    reg = Emit_StackPop(rec);
    if (need_swap) {
	unsigned inst_size = entry_bb->insts.size;
	assert(inst_size >= 2);
	basicblock_swap_inst(entry_bb, inst_size - 2, inst_size - 1);
    }
    rec->cur_bb = cur_bb;
    return reg;
}

static VALUE *trace_invoke(rb_jit_t *jit, jit_event_t *e, trace_t *trace)
{
    return e->pc;
}

static int peephole(trace_recorder_t *rec, lir_inst_t *inst)
{
    if (lir_is_guard(inst) && elimnate_guard(rec, inst)) {
	// guard can remove
	return 1;
    }
    return 0;
}

static lir_inst_t *constant_fold_inst(trace_recorder_t *recorder, lir_inst_t *inst);

static void dump_lir_inst(lir_inst_t *inst);

static lir_inst_t *trace_recorder_add_inst(trace_recorder_t *recorder, lir_inst_t *inst, unsigned inst_size)
{
    lir_inst_t *newinst = NULL;
    if (peephole(recorder, inst)) {
	return NULL;
    }
    if ((newinst = constant_fold_inst(recorder, inst)) == inst) {
	// when `inst` is able to constant folding, folded `inst`
	// is already inserted by `constant_fold_inst`
	newinst = lir_inst_allocate(recorder, inst, inst_size);
	basicblock_append(recorder->cur_bb, newinst);
    }
    if (DUMP_LIR) {
	dump_lir_inst(newinst);
    }
    //update_userinfo(Rec, buf);
    return newinst;
}

/* } trace_recorder */

/* rb_jit { */
static void jit_global_default_params_setup(struct rb_vm_global_state *global_state_ptr)
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
    {
	int i;
	for (i = 0; i < BOP_LAST_; i++) {
	    jit_vm_redefined_flag[i] = global_state_ptr->_ruby_vm_redefined_flag[i];
	}
    }
    jit_runtime.redefined_flag = jit_vm_redefined_flag;
    jit_runtime.global_method_state = global_state_ptr->_global_method_state;
    jit_runtime.global_constant_state = global_state_ptr->_global_constant_state;
    jit_runtime.class_serial = global_state_ptr->_class_serial;
}

static void jit_default_params_setup(rb_jit_t *jit)
{
}

static void jit_mark(void *ptr)
{
    RUBY_MARK_ENTER("jit");
    if (ptr) {
    }
    RUBY_MARK_LEAVE("jit");
}

static size_t jit_memsize(const void *ptr)
{
    size_t size = 0;
    if (ptr) {
    }
    return size;
}

static const rb_data_type_t jit_data_type = {
    "JIT",
    {
      jit_mark, NULL, jit_memsize,
    },
    NULL,
    NULL,
    RUBY_TYPED_FREE_IMMEDIATELY
};

static rb_jit_t *jit_init()
{
    rb_jit_t *jit = (rb_jit_t *)malloc(sizeof(*jit));
    VALUE rb_cJit = rb_define_class("Jit", rb_cObject);
    rb_undef_alloc_func(rb_cJit);
    rb_undef_method(CLASS_OF(rb_cJit), "new");
    jit->self = TypedData_Wrap_Struct(rb_cJit, &jit_data_type, jit);
    rb_gc_register_mark_object(jit->self);

    jit->recorder = trace_recorder_new();
    hashmap_init(&jit->traces, 1);
    rb_jit_init_redefined_flag();
    jit_default_params_setup(jit);
    return jit;
}

static void jit_delete(rb_jit_t *jit)
{
    if (jit) {
	// FIXME need to free allocated traces
	hashmap_dispose(&jit->traces, NULL);
	trace_recorder_delete(jit->recorder);
	free(jit);
    }
}

void Init_rawjit(struct rb_vm_global_state *global_state_ptr)
{
    jit_global_default_params_setup(global_state_ptr);
    if (!disable_jit) {
	rb_cMath = rb_singleton_class(rb_mMath);
	current_jit = jit_init();
	Init_jit(); // load jit_prelude
    }
}

void Destruct_rawjit()
{
    if (!disable_jit) {
	jit_delete(current_jit);
	current_jit = NULL;
    }
    // remove compiler warnings
    (void)insn_op_types;
    (void)insn_op_type;
}
/* } rb_jit */

static int is_recording(rb_jit_t *jit)
{
    return (jit->mode &= TraceModeRecording) == TraceModeRecording;
}

static void set_recording(rb_jit_t *jit, trace_t *trace)
{
    jit->mode |= TraceModeRecording;
    jit->current_trace = trace;
}

static void stop_recording(rb_jit_t *jit)
{
    jit->mode = TraceModeDefault;
    jit->current_trace = NULL;
}

static trace_t *create_new_trace(rb_jit_t *jit, jit_event_t *e, trace_side_exit_handler_t *parent)
{
    rb_control_frame_t *reg_cfp = e->cfp;
    trace_t *trace = (trace_t *)malloc(sizeof(*trace));
    memset(trace, 0, sizeof(*trace));
    trace->start_pc = e->pc;
    trace->parent = parent;
    trace->iseq = GET_ISEQ();
    const_pool_init(&trace->cpool);
    if (!parent) {
	hashmap_set(&jit->traces, (hashmap_data_t)e->pc, (hashmap_data_t)trace);
    }
    return trace;
}

static trace_t *find_trace(rb_jit_t *jit, jit_event_t *e)
{
    return (trace_t *)hashmap_get(&jit->traces, (hashmap_data_t)e->pc);
}

static int is_backward_branch(jit_event_t *e, VALUE **target_pc_ptr)
{
    OFFSET dst;
    rb_control_frame_t *reg_cfp = e->cfp;
    if (e->opcode != BIN(branchif)) {
	return 0;
    }
    dst = (OFFSET)GET_OPERAND(1);
    *target_pc_ptr = e->pc + insn_len(BIN(branchif)) + dst;
    return dst < 0;
}

static int is_compiled_trace(trace_t *trace)
{
    return trace && trace->handler && trace->code;
}

static int already_recorded_on_trace(jit_event_t *e)
{
    rb_jit_t *jit = current_jit;
    trace_side_exit_handler_t *parent = e->trace->parent;
    trace_t *trace;
    if (parent) {
	if (parent->this_trace->start_pc == e->pc) {
	    // linked to other trace
	    // TODO emit exit
	    return 1;
	}
    }
    else if (e->trace->start_pc == e->pc) {
	// TODO formed loop.
	return 1;
    }
    else if ((trace = find_trace(jit, e)) != NULL && is_compiled_trace(trace)) {
	// linked to other trace
	// TODO emit exit
	return 1;
    }
    return 0;
}

// yarv2lir.c
static int is_tracable_special_inst(int opcode, CALL_INFO ci);

static int is_tracable_call_inst(jit_event_t *e)
{
    rb_control_frame_t *reg_cfp;
    CALL_INFO ci;
    if (e->opcode != BIN(send) && e->opcode != BIN(opt_send_simple)) {
	return 1;
    }

    reg_cfp = e->cfp;
    ci = (CALL_INFO)GET_OPERAND(1);
    vm_search_method(ci, ci->recv = TOPN(ci->argc));
    if (ci->me) {
	switch (ci->me->def->type) {
	    case VM_METHOD_TYPE_IVAR:
	    case VM_METHOD_TYPE_ATTRSET:
	    case VM_METHOD_TYPE_ISEQ:
		return 1;
	    default:
		break;
	}
    }

    /* check Class.A.new(argc, argv) */
    if (check_cfunc(ci->me, rb_class_new_instance) && ci->me->klass == rb_cClass) {
	return 1;
    }
    /* check block_given? */
    if (check_cfunc(ci->me, rb_f_block_given_p)) {
	return 1;
    }

    if (is_tracable_special_inst(e->opcode, ci)) {
	return 1;
    }
    return 0;
}

static int is_irregular_event(jit_event_t *e)
{
    return e->opcode == BIN(throw);
}

static int is_end_of_trace(trace_recorder_t *recorder, jit_event_t *e)
{
    if (already_recorded_on_trace(e)) {
	e->reason = TRACE_ERROR_ALREADY_RECORDED;
	return 1;
    }
    if (trace_recorder_is_full(recorder)) {
	e->reason = TRACE_ERROR_BUFFER_FULL;
	// TODO emit exit
	return 1;
    }
    if (!is_tracable_call_inst(e)) {
	e->reason = TRACE_ERROR_NATIVE_METHOD;
	// TODO emit exit
	return 1;
    }
    if (is_irregular_event(e)) {
	e->reason = TRACE_ERROR_THROW;
	// TODO emit exit
	return 1;
    }
    return 0;
}

static void trace_reset(trace_t *trace, int alloc_memory)
{
}

static void record_insn(trace_recorder_t *ecorder, jit_event_t *e);

static VALUE *trace_selection(rb_jit_t *jit, jit_event_t *e)
{
    trace_t *trace = NULL;
    VALUE *target_pc = NULL;
    if (is_recording(jit)) {
	if (is_end_of_trace(jit->recorder, e)) {
	    stop_recording(jit);
	    record_insn(jit->recorder, e);
	    compile_trace(jit, jit->recorder);
	}
	else {
	    record_insn(jit->recorder, e);
	}
	return e->pc;
    }
    trace = find_trace(jit, e);
    if (is_compiled_trace(trace)) {
	return trace_invoke(jit, e, trace);
    }
    if (is_backward_branch(e, &target_pc)) {
	if (!trace) {
	    trace = create_new_trace(jit, e, NULL);
	}
	trace->last_pc = target_pc;
    }
    if (trace) {
	trace->counter += 1;
	if (trace->counter > HOT_TRACE_THRESHOLD) {
	    set_recording(jit, trace);
	    trace_reset(trace, 1);
	    trace_recorder_clear(jit->recorder, trace, 1);
	    record_insn(jit->recorder, e);
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

#define FMT(T) FMT_##T
#define FMT_int "%d"
#define FMT_long "%ld"
#define FMT_lir_t "%04ld"
#define FMT_LirPtr "%04ld"
#define FMT_ID "%04ld"
#define FMT_VALUE "0x%lx"
#define FMT_VALUEPtr "%p"
#define FMT_voidPtr "%p"
#define FMT_CALL_INFO "%p"
#define FMT_IC "%p"
#define FMT_ISEQ "%p"
#define FMT_BasicBlockPtr "bb:%d"
#define FMT_rb_event_flag_t "%lu"

#define DATA(T, V) DATA_##T(V)
#define DATA_int(V) (V)
#define DATA_long(V) (V)
#define DATA_lir_t(V) (lir_getid(V))
#define DATA_LirPtr(V) (lir_getid(*(V)))
#define DATA_VALUE(V) (V)
#define DATA_VALUEPtr(V) (V)
#define DATA_voidPtr(V) (V)
#define DATA_CALL_INFO(V) (V)
#define DATA_IC(V) (V)
#define DATA_ISEQ(V) (V)
#define DATA_BasicBlockPtr(V) ((V)->base.id)
#define DATA_rb_event_flag_t(V) (V)

#define LIR_NEWINST(T) ((T *)lir_inst_init(alloca(sizeof(T)), OPCODE_##T))
#define LIR_NEWINST_N(T, SIZE) \
    ((T *)lir_inst_init(alloca(sizeof(T) + sizeof(lir_t) * (SIZE)), OPCODE_##T))

#define ADD_INST(REC, INST) ADD_INST_N(REC, INST, 0)

#define ADD_INST_N(REC, INST, SIZE) \
    trace_recorder_add_inst(REC, &(INST)->base, sizeof(*INST) + sizeof(lir_t) * (SIZE))

typedef lir_t *LirPtr;
typedef VALUE *VALUEPtr;
typedef void *voidPtr;
typedef basicblock_t *BasicBlockPtr;
typedef void *lir_folder_t;

#include "lir_template.h"
#include "lir.c"

static int lir_is_terminator(lir_inst_t *inst)
{
    switch (inst->opcode) {
#define IS_TERMINATOR(OPNAME) \
    case OPCODE_I##OPNAME:    \
	return LIR_IS_TERMINATOR_##OPNAME;
	LIR_EACH(IS_TERMINATOR);
	default:
	    assert(0 && "unreachable");
#undef IS_TERMINATOR
    }
    return 0;
}

static int lir_is_guard(lir_inst_t *inst)
{
    switch (inst->opcode) {
#define IS_TERMINATOR(OPNAME) \
    case OPCODE_I##OPNAME:    \
	return LIR_IS_GUARD_INST_##OPNAME;
	LIR_EACH(IS_TERMINATOR);
	default:
	    assert(0 && "unreachable");
#undef IS_TERMINATOR
    }
    return 0;
}

static void dump_lir_inst(lir_inst_t *inst)
{
    if (DUMP_LIR > 0) {
	switch (inst->opcode) {
#define DUMP_IR(OPNAME)      \
    case OPCODE_I##OPNAME:   \
	Dump_##OPNAME(inst); \
	break;
	    LIR_EACH(DUMP_IR);
	    default:
		assert(0 && "unreachable");
#undef DUMP_IR
	}
    }
}

static void dump_lir_block(basicblock_t *block)
{
    if (DUMP_LIR > 0) {
	unsigned i = 0;
	fprintf(stderr, "BB%ld (pc=%p)\n", lir_getid(&block->base), block->start_pc);
	for (i = 0; i < block->insts.size; i++) {
	    lir_inst_t *inst = (lir_inst_t *)block->insts.list[i];
	    dump_lir_inst(inst);
	}
    }
}

static void dump_side_exit(trace_recorder_t *rec)
{
    if (DUMP_LIR > 0) {
	int i;
	hashmap_iterator_t itr = { 0, 0 };
	while (hashmap_next(&rec->stack_map, &itr)) {
	    VALUE *pc = (VALUE *)itr.entry->key;
	    regstack_t *stack = (regstack_t *)itr.entry->val;
	    fprintf(stderr, "side exit(size=%04d): pc=%p: ", stack->list.size - LIR_RESERVED_REGSTACK_SIZE, pc);
	    for (i = 0; i < stack->list.size; i++) {
		lir_t inst = (lir_t)jit_list_get(&stack->list, i);
		if (inst) {
		    fprintf(stderr, "  [%d] = %04ld;", i - LIR_RESERVED_REGSTACK_SIZE, lir_getid(inst));
		}
	    }
	    fprintf(stderr, "\n");
	}
    }
}

static void dump_trace(trace_recorder_t *rec)
{
    if (DUMP_LIR > 0) {
	unsigned i;
	for (i = 0; i < rec->bblist.size; i++) {
	    basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, i);
	    fprintf(stderr, "---------------\n");
	    dump_lir_block(bb);
	}
	fprintf(stderr, "---------------\n");
	dump_side_exit(rec);
	fprintf(stderr, "---------------\n");
    }
}

#include "jit_record.c"
#include "jit_optimize.c"
