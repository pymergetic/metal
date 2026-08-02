{% for f in fns %}static __PM_METAL_IMPORT_{{ f.name }}: pymergetic_metal_reg::ImportRow = pymergetic_metal_reg::ImportRow::new("{{ module_name }}", "{{ f.name }}");
{% endfor %}
