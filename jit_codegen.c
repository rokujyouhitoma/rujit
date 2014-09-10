/**********************************************************************

  jit_codegen.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

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
    char *ptr = (char *)buf->buf.list + size;
    size_t n = vsnprintf(ptr, capacity - size, fmt, ap);
    if (n + size < capacity) {
	buf->buf.size += size;
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
	snprintf(fpath, 512, "/tmp/gwjit.%d.%d.c", getpid(), id);
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
	snprintf(fpath, 512, "/tmp/gwjit.%d.%d.c", getpid(), id);
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
	int (*finit)(const void *jit_context, trace_t *this_trace);
	char fname2[128] = {};
	snprintf(fname2, 128, "init_%s", fname);
	finit = dlsym(gen->hdr, fname2);
	if (finit) {
	    finit(NULL, trace);
	    trace->code = dlsym(gen->hdr, fname);
	}
    }
    return NULL;
}
