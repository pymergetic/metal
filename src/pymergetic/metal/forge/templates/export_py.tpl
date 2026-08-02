"""Stubs for {{ module_name }}."""

{% for f in fns %}def {{ f.name }}({{ f.args }}) -> int: ...
{% endfor %}{% if not fns %}# package marker (no exported symbols)
{% endif %}
