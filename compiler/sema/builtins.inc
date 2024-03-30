#ifndef BUILTIN_TYPE
#define BUILTIN_TYPE(name) BUILTIN(name)
#endif

BUILTIN_TYPE(error_value)

BUILTIN_TYPE(type_type)
BUILTIN_TYPE(namespace_type)

BUILTIN_TYPE(template_signature_type)
BUILTIN(template_id_template)

BUILTIN_TYPE(function_signature_type)
BUILTIN(function_id_template)

#undef BUILTIN_TYPE
#undef BUILTIN