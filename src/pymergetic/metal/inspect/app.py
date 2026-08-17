"""Inspect Microdot app — same stubs, attached to ASGI listen."""
from .adapter_microdot import MicrodotAdapter


def create_app():
    adapter = MicrodotAdapter(role="metal", theme="metal")
    adapter.attach_asgi()
    return adapter.app
