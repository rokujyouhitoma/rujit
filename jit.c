/**********************************************************************

  jit.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

#include <ruby/ruby.h>

typedef struct rb_jit_t {
    void *p;
} rb_jit_t;

void Init_jit()
{
}

void Destruct_jit()
{
}

static void jit_default_params_setup(rb_jit_t *jit)
{
}
