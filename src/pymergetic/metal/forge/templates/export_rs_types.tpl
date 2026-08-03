{% for d in defines %}{{ d.line }}
{% endfor %}{% if defines %}
{% endif %}{% for en in enums %}#[repr(u32)]
#[derive(Clone, Copy)]
#[allow(non_camel_case_types)]
pub enum {{ en.name }} {
{% for v in en.variants %}    {{ v.name }} = {{ v.value }},
{% endfor %}}

{% endfor %}{% for st in structs %}#[repr(C)]
#[derive(Clone, Copy)]
pub {{ st.kind }} {{ st.name }} {
{% for f in st.fields %}    pub {{ f.name }}: {{ f.ty }},
{% endfor %}}

{% endfor %}{% for td in typedefs %}pub type {{ td.name }} = {{ td.rs }};
{% endfor %}{% if typedefs %}
{% endif %}