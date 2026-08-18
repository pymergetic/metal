"""Inspect Microdot app — same stubs, attached to ASGI listen."""
from .adapter_microdot import MicrodotShell


def create_app():
    shell = MicrodotShell(role="metal", theme="metal")
    shell.attach_asgi()
    return shell.app
