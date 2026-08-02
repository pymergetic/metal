extern "C" {
{% for f in fns %}    {{ f.decl }}
{% endfor %}{% if module_empty %}    // module {{ module_name }}: empty catalog
{% endif %}}

