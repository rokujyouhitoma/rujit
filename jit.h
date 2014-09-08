/**********************************************************************

  jit.h -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#ifndef RUBY_JIT_H
#define RUBY_JIT_H 1

extern VALUE *rb_jit_trace(rb_thread_t *, rb_control_frame_t *, VALUE *);

#endif /* end of include guard */
