# Catalog HTTP/JSON consumers — register routes on the Microdot app.


def register(app):
  import api.home as home
  import api.docs as docs
  import api.iface as iface
  import api.externals as externals
  import api.limits as limits

  home.register(app)
  docs.register(app)
  iface.register(app)
  externals.register(app)
  limits.register(app)
