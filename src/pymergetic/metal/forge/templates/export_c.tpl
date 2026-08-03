#ifndef {{ guard }}
#define {{ guard }}

#include <stddef.h> /* IWYU pragma: keep */
#include <stdint.h> /* IWYU pragma: keep */
{% if guest_surface %}#include <pymergetic/metal/pkg_import.h> /* IWYU pragma: keep */
{% endif %}{% for inc in includes %}#include <{{ inc }}> /* IWYU pragma: keep */
{% endfor %}
#ifdef __cplusplus
extern "C" {
#endif

{% for d in defines %}#define {{ d.name }} {{ d.value }}
{% endfor %}{% if defines %}
{% endif %}{% for st in structs %}typedef struct {{ st.name }} {{ st.name }};

{% endfor %}{% for en in enums %}typedef enum {
{% for v in en.variants %}  {{ v.name }} = {{ v.value }}{{ v.comma }}
{% endfor %}} {{ en.name }};

{% endfor %}{% for td in typedefs %}typedef {{ td.line }};

{% endfor %}{% for name in foreign_types %}typedef struct {{ name }} {{ name }};

{% endfor %}{% for st in structs %}struct {{ st.name }} {
{% for f in st.fields %}  {{ f.line }};
{% endfor %}};

{% endfor %}{% for f in fns %}{% if guest_surface %}#if defined(__wasm__)
PM_METAL_PKG_IMPORT("{{ module_name }}", {{ f.name }})
extern {{ f.ret }} {{ f.name }}({{ f.args }});
#else
{{ f.ret }} {{ f.name }}({{ f.args }});
#endif

{% else %}{{ f.ret }} {{ f.name }}({{ f.args }});
{% endif %}{% endfor %}{% if empty %}/* module {{ module_name }}: empty catalog */
{% endif %}
#ifdef __cplusplus
}
#endif

#endif /* {{ guard }} */

