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

void rb_gc_writebarrier(VALUE a, VALUE b)
{
    return JIT_RUNTIME->_rb_gc_writebarrier(a, b);
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

#endif
#include "lir_template.h"
#endif /* end of include guard */
