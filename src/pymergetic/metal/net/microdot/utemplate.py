"""pymergetic.metal.net.microdot.utemplate — vendored microdot utemplate extension.

Microdot's server-side templating integration (upstream), backed by the vendored
`utemplate` engine under `pymergetic.metal.net.microdot._utemplate` (the real
pfalcon/utemplate engine). The inspect app never renders server-side templates
(the frontend is the embedded SPA), so nothing imports this; it is vendored for
the complete microdot surface.
"""

try:
    from ._utemplate import recompile  # the vendored engine

    _HAS_UTEMPLATE = True
except Exception:  # pragma: no cover — engine unavailable
    recompile = None
    _HAS_UTEMPLATE = False


class Template:
    """A template object.

    :param template: The filename of the template to render, relative to the
    configured template directory.
    """

    @classmethod
    def initialize(cls, template_dir="templates",
                    loader_class=None):
        """Initialize the templating subsystem.

        :param template_dir: the directory where templates are stored. This
        argument is optional. The default is to load
        templates from a *templates* subdirectory.
        :param loader_class: the ``utemplate.Loader`` class to use when loading
        templates. If omitted, ``recompile.Loader`` is used.
        """
        global _loader
        if not _HAS_UTEMPLATE:
            raise RuntimeError("utemplate engine not vendored")
        # `recompile` is a module global reassigned to None on import failure;
        # the flag guard above ensures it is the real engine here, so narrow it
        # for the type checker too.
        assert recompile is not None
        cls.loader_class = loader_class or recompile.Loader
        _loader = cls.loader_class(None, template_dir)

    def __init__(self, template):
        if _loader is None:  # pragma: no cover
            self.initialize()
        #: The name of the template
        self.name = template
        assert _loader is not None  # initialize() above installs the loader
        self.template = _loader.load(template)

    def generate(self, *args, **kwargs):
        """Return a generator that renders the template in chunks, with the
        given arguments."""
        return self.template(*args, **kwargs)

    def render(self, *args, **kwargs):
        """Render the template with the given arguments and return it as a
        string."""
        return ''.join(self.generate(*args, **kwargs))

    def generate_async(self, *args, **kwargs):
        """Return an asynchronous generator that renders the template in
        chunks, using the given arguments."""
        class sync_to_async_iter():
            def __init__(self, iter):
                self.iter = iter

            def __aiter__(self):
                return self

            async def __anext__(self):
                try:
                    return next(self.iter)
                except StopIteration:
                    raise StopAsyncIteration

        return sync_to_async_iter(self.generate(*args, **kwargs))

    async def render_async(self, *args, **kwargs):
        """Render the template with the given arguments asynchronously and
        return it as a string."""
        response = ''
        async for chunk in self.generate_async(*args, **kwargs):
            response += chunk
        return response


_loader = None
