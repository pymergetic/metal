"""Inspect Microdot app — mounts shared stubs via MicrodotAdapter."""
from .adapter_microdot import MicrodotAdapter


def create_app():
    return MicrodotAdapter(role="metal", theme="metal").app
