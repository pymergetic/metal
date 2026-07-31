# Shared-context autoexec (mods/<name>/autoload.py) — guest stack identity.
import pymergetic.metal.externals as ext

ext.register(
    "microdot",
    "2.6.2",
    "https://github.com/miguelgrinberg/microdot",
    "ASGI Microdot (guest httpd)",
)
ext.register(
    "utemplate",
    "microdot-bundled",
    "https://github.com/pfalcon/utemplate",
    "utemplate HTML (guest httpd/api)",
)
