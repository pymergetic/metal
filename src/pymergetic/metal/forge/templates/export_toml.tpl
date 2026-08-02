{% for st in structs %}[[struct]]
name = {{ st.name }}
{% if st.fields %}fields = [
{% for f in st.fields %}  { name = {{ f.name }}, ty = {{ f.ty }} },
{% endfor %}]
{% endif %}
{% endfor %}{% for td in typedefs %}[[typedef]]
name = {{ td.name }}
ty = {{ td.ty }}

{% endfor %}{% for en in enums %}[[enum]]
name = {{ en.name }}
variants = [
{% for v in en.variants %}  { name = {{ v.name }}, value = {{ v.value }} },
{% endfor %}]

{% endfor %}{% for f in fns %}[[fn]]
name = {{ f.name }}
ret = {{ f.ret }}
{% if f.inline %}inline = true
{% endif %}{% if f.args %}args = [
{% for a in f.args %}  { name = {{ a.name }}, ty = {{ a.ty }} },
{% endfor %}]
{% endif %}
{% endfor %}{% if empty %}# empty catalog

{% endif %}