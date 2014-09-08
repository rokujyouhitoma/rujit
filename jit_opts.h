/**********************************************************************

  jit_opts.h -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#ifndef JIT_OPTS_H
#define JIT_OPTS_H

// When `USE_CGEN` is defined,  we use c-generator as backend of jit compiler.
// If `USE_LLVM` is defined, use llvm as backend.
#define USE_CGEN 1
//#define USE_LLVM 1

#define DUMP_STACK_MAP 0 /* 0:disable, 1:dump, 2:verbose */
//#define DUMP_LLVM_IR   0 /* 0:disable, 1:dump, 2:dump non-optimized llvm ir */
#define DUMP_INST 1 /* 0:disable, 1:dump */
#define DUMP_LIR 1 /* 0:disable, 1:dump */
#define DUMP_CALL_STACK_MAP 0 /* 0:disable, 1:dump */

#define JIT_DUMP_COMPILE_LOG 2 /* 0:disable, 1:dump, 2:verbose */
#define LIR_MIN_TRACE_LENGTH 8 /* min length of instructions jit compile */
#define LIR_MAX_TRACE_LENGTH 1024 /* max length of instructions jit compile */
#define LIR_TRACE_INIT_SIZE 16 /* initial size of trace */
#define LIR_RESERVED_REGSTACK_SIZE 8

#define JIT_LOG_SIDE_EXIT  0 /* 0:disable, 1: emit log if side exit occured */

/* Initial buffer size of lir memory allocator */
#define LIR_COMPILE_DATA_BUFF_SIZE (512)

#endif /* end of include guard */
