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

// rujit
static int disable_jit = 0;

typedef struct rb_jit_t rb_jit_t;

typedef struct jit_trace trace_t;

struct memory_pool {
    struct page_chunk *head;
    struct page_chunk *root;
    unsigned pos;
    unsigned size;
};

typedef struct const_pool {
    uintptr_t *list;
    unsigned size;
    unsigned capacity;
} const_pool_t;

#define HOT_TRACE_THRESHOLD 4
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

typedef struct trace_recorder_t {
    jit_event_t *current_event;
    //basicblock_t *cur_bb;
    //basicblock_t *entry_bb;
    struct lir_inst_t **insts;
    unsigned inst_size;
    unsigned flag;
    struct memory_pool mp;
} trace_recorder_t;

typedef enum trace_mode {
    TraceModeDefault = 0,
    TraceModeRecording = 1,
    TraceModeError = -1
} trace_mode_t;

struct rb_jit_t {
    VALUE self;
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

    jit_default_params_setup(jit);
    return jit;
}

static void jit_delete(rb_jit_t *jit)
{
    if (jit) {
	free(jit);
    }
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
    // remove compiler warnings
    (void)insn_op_types;
    (void)insn_op_type;
}

/* memory pool { */
#define SIZEOF_MEMORY_POOL (4096 - sizeof(void *))
struct page_chunk {
    struct page_chunk *next;
    char buf[SIZEOF_MEMORY_POOL];
};

static void memory_pool_init(struct memory_pool *mp)
{
    mp->pos = 0;
    mp->size = SIZEOF_MEMORY_POOL;
    mp->head = mp->root = NULL;
}

static void memory_pool_reset(struct memory_pool *mp, int alloc_memory)
{
    struct page_chunk *root = mp->root;
    struct page_chunk *page = root->next;
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
    mp->head = mp->root = root;
}

static void *memory_pool_alloc(struct memory_pool *mp, size_t size)
{
    void *ptr;
    if (mp->pos + size > mp->size) {
	struct page_chunk *page;
	page = (struct page_chunk *)malloc(sizeof(struct page_chunk));
	page->next = NULL;
	mp->head->next = page;
	mp->head = page;
	mp->pos = 0;
    }
    ptr = mp->head->buf + mp->pos;
    memset(ptr, 0, size);
    mp->pos += size;
    return ptr;
}
/* } memory pool */

/* const pool { */
#define CONST_POOL_API static

#define CONST_POOL_INIT_SIZE 1
CONST_POOL_API const_pool_t *const_pool_init(const_pool_t *self)
{
    self->list = NULL;
    self->size = 0;
    self->capacity = 0;
    return self;
}

CONST_POOL_API int const_pool_contain(const_pool_t *self, const void *ptr)
{
    unsigned i;
    for (i = 0; i < self->size; i++) {
	if (self->list[i] == (uintptr_t)ptr) {
	    return 1;
	}
    }
    return 0;
}

CONST_POOL_API void const_pool_add(const_pool_t *self, const void *ptr)
{
    if (const_pool_contain(self, ptr)) {
	return;
    }
    if (self->size + 1 >= self->capacity) {
	if (self->capacity == 0) {
	    self->capacity = CONST_POOL_INIT_SIZE;
	    self->list = (uintptr_t *)malloc(sizeof(uintptr_t) * self->capacity);
	}
	else {
	    unsigned newcapacity;
	    self->capacity *= 2;
	    newcapacity = sizeof(uintptr_t) * self->capacity;
	    self->list = (uintptr_t *)realloc(self->list, newcapacity);
	}
    }
    self->list[self->size++] = (uintptr_t)ptr;
}

CONST_POOL_API void const_pool_delete(const_pool_t *self)
{
    if (self->list) {
	free(self->list);
    }
    self->size = self->capacity = 0;
}
/* } const pool */

static int is_recording(rb_jit_t *jit)
{
    return (jit->mode &= TraceModeRecording) == TraceModeRecording;
}

static void set_recording(rb_jit_t *jit)
{
    jit->mode |= TraceModeRecording;
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

static int trace_recorder_is_full(trace_recorder_t *recorder)
{
    return recorder->inst_size - 1 == LIR_MAX_TRACE_LENGTH;
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

    //if (is_tracable_special_inst(e->opcode, ci)) {
    //    return 1;
    //}
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
    if (is_tracable_call_inst(e)) {
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

static void append_to_tracebuffer(trace_recorder_t *rec, jit_event_t *e)
{
}

static void compile_trace(rb_jit_t *jit, trace_recorder_t *recorder)
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
	if (is_end_of_trace(jit->recorder, e)) {
	    compile_trace(jit, jit->recorder);
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
