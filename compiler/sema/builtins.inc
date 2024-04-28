#ifndef BUILTIN_TYPE
#define BUILTIN_TYPE(name) BUILTIN(name, name##_type)
#endif
#ifndef BUILTIN_TEMPLATE
#define BUILTIN_TEMPLATE(name) BUILTIN(name, name##_template)
#endif

BUILTIN_TYPE(type)

BUILTIN_TYPE(error)

BUILTIN_TYPE(namespace)

BUILTIN_TYPE(template_signature)
BUILTIN_TEMPLATE(template_id)

BUILTIN_TYPE(function_signature)
BUILTIN_TEMPLATE(function_id)

#undef BUILTIN_TYPE
#undef BUILTIN_TEMPLATE
#undef BUILTIN