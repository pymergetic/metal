"""OpenAPI (FastAPI-style) for the seat's Microdot/asgi HTTP API.

The CDN serves interactive docs (swagger-ui over /openapi.json); the seat serves
the same face from its own routes. This module is the emitter: a module registry
of the seat's *real* routes — health/capabilities/self/reg, the CDN-shaped
artifact+package introspect API, and the RPC call face — rendered into an
OpenAPI 3.x document at /openapi.json, plus the /docs page that loads swagger-ui
from the same CDN tag the CDN's docs template uses and points it at /openapi.json.

Served as deferred routes (like /packs/* and /artifacts/*) by metal_packs's pump,
so no web framework is required to take a seat there.
"""

try:
    import json
except ImportError:  # pragma: no cover - MicroPython
    import ujson as json

_OPENAPI_VERS = "3.0.3"
_TITLE = "pymergetic.metal — seat API"
_VERSION = "0.1.0"


def _refs():
    """Shared response component schemas (kept tiny — the seat value is the live doc)."""
    return {
        "Health": {
            "type": "object",
            "required": ["ok"],
            "properties": {"ok": {"type": "boolean"}},
        },
        "Error": {
            "type": "object",
            "required": ["detail"],
            "properties": {"detail": {"type": "string"}},
        },
        "RegistryLedger": {
            "type": "object",
            "properties": {
                "schema": {"type": "integer"},
                "method_count": {"type": "integer"},
                "gap_count": {"type": "integer"},
                "modules": {"type": "array", "items": {"type": "string"}},
                "methods": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "module": {"type": "string"},
                            "func": {"type": "string"},
                            "exports": {"type": "integer"},
                        },
                    },
                },
                "gaps": {"type": "array"},
                "note": {"type": "string"},
            },
        },
        "ModuleExports": {
            "type": "object",
            "properties": {
                "module": {"type": "string"},
                "export_count": {"type": "integer"},
                "exports": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "name": {"type": "string"},
                            "kind": {"type": "string"},
                            "sig": {"type": "string"},
                        },
                    },
                },
            },
        },
        "ArtifactInspected": {
            "type": "object",
            "properties": {
                "kind": {"type": "string", "enum": ["card"]},
                "encoding": {"type": "string", "enum": ["registry"]},
                "signed": {"type": "boolean"},
                "naked_size": {"type": "integer"},
                "size": {"type": "integer"},
                "has_dwarf": {"type": "boolean"},
                "sections": {"type": "array"},
                "symbols": {"type": "array"},
                "exports": {"type": "array"},
                "pack": {"type": "object", "nullable": True},
                "source": {"type": "object", "nullable": True},
            },
        },
        "PackageCatalog": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {"name": {"type": "string"}, "role": {"type": "string"}},
            },
        },
        "Versions": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "channel": {"type": "string"},
                    "version": {"type": "string"},
                    "label": {"type": "string"},
                    "artifact_count": {"type": "integer"},
                },
            },
        },
        "PackageDetail": {
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "channel": {"type": "string"},
                "role": {"type": "string"},
                "artifacts": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {"path": {"type": "string"}},
                    },
                },
            },
        },
    }


def _param(name, where, req, schema):
    p = {
        "name": name,
        "in": where,
        "required": req,
        "schema": schema,
    }
    return p


def _op(summary, ok, params=None, tags=None):
    op = {
        "summary": summary,
        "operationId": summary.replace(" ", "_").replace("/", "_"),
        "tags": tags or ["API"],
        "responses": {
            "200": {"description": "OK", "content": {"application/json": {"schema": ok}}},
            "404": {"description": "Not found", "content": {
                "application/json": {"schema": {"$ref": "#/components/schemas/Error"}}}},
        },
    }
    if params:
        op["parameters"] = params
    return op


def build():
    """Build the full OpenAPI document for the seat's served HTTP API."""
    artifact_params = [
        _param("artifact", "path", True, {"type": "string"}),
    ]
    return {
        "openapi": _OPENAPI_VERS,
        "info": {"title": _TITLE, "version": _VERSION},
        "paths": {
            "/health": {
                "get": _op("health check", {"$ref": "#/components/schemas/Health"},
                           tags=["system"]),
            },
            "/capabilities": {
                "get": _op("capabilities", {"type": "object"}, tags=["system"]),
            },
            "/inspect/self": {
                "get": _op("self description", {"type": "object"}, tags=["inspect"]),
            },
            "/inspect/reg": {
                "get": _op(
                    "registry ledger",
                    {"$ref": "#/components/schemas/RegistryLedger"},
                    tags=["inspect"],
                ),
            },
            "/inspect/reg/{module}": {
                "get": _op(
                    "module exports",
                    {"$ref": "#/components/schemas/ModuleExports"},
                    params=[_param("module", "path", True, {"type": "string"})],
                    tags=["inspect"],
                ),
            },
            "/inspect/call/{module}/{method}": {
                "get": _op(
                    "invoke a container export (RPC)",
                    {"type": "object"},
                    params=[
                        _param("module", "path", True, {"type": "string"}),
                        _param("method", "path", True, {"type": "string"}),
                    ],
                    tags=["inspect"],
                ),
            },
            "/artifacts/lead/{artifact}/inspect": {
                "get": _op(
                    "artifact inspect digest",
                    {"$ref": "#/components/schemas/ArtifactInspected"},
                    params=artifact_params,
                    tags=["artifacts"],
                ),
            },
            "/artifacts/lead/{artifact}/sections": {
                "get": _op(
                    "artifact sections",
                    {"type": "array"},
                    params=artifact_params,
                    tags=["artifacts"],
                ),
            },
            "/artifacts/lead/{artifact}/symbols": {
                "get": _op(
                    "artifact symbols",
                    {"type": "array"},
                    params=artifact_params,
                    tags=["artifacts"],
                ),
            },
            "/artifacts/lead/{artifact}/disasm": {
                "get": _op(
                    "artifact disassembly",
                    {"type": "array"},
                    params=artifact_params,
                    tags=["artifacts"],
                ),
            },
            "/packages": {
                "get": _op(
                    "package catalog",
                    {"$ref": "#/components/schemas/PackageCatalog"},
                    tags=["packages"],
                ),
            },
            "/packages/{name}": {
                "get": _op(
                    "package artifact list",
                    {"$ref": "#/components/schemas/PackageDetail"},
                    params=[_param("name", "path", True, {"type": "string"})],
                    tags=["packages"],
                ),
            },
            "/packages/{name}/versions": {
                "get": _op(
                    "package versions",
                    {"$ref": "#/components/schemas/Versions"},
                    params=[_param("name", "path", True, {"type": "string"})],
                    tags=["packages"],
                ),
            },
        },
        "components": {"schemas": _refs()},
    }


def openapi_json_bytes():
    return json.dumps(build(), separators=(",", ":")).encode()


def docs_html():
    """The /docs page — reuses the same swagger-ui CDN tag as the CDN's docs.html."""
    return (
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<title>pymergetic.metal — API</title>"
        "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui.css\" crossorigin>"
        "</head><body>"
        "<section class=\"docs-hero\"><p class=\"eyebrow\">OpenAPI</p>"
        "<h1>API</h1>"
        "<p class=\"lede\">Interactive schema for the seat's inspect, artifact, and package API.</p></section>"
        "<div id=\"swagger-ui\" class=\"swagger-host\"></div>"
        "<script src=\"https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui-bundle.js\" crossorigin></script>"
        "<script>window.ui = SwaggerUIBundle({"
        "url: \"/openapi.json\","
        "dom_id: \"#swagger-ui\","
        "deepLinking: true,"
        "presets: [SwaggerUIBundle.presets.apis],"
        "layout: \"BaseLayout\","
        "tryItOutEnabled: true,"
        "persistAuthorization: true,"
        "syntaxHighlight: { activated: true, theme: \"monokai\" },"
        "docExpansion: \"list\","
        "defaultModelsExpandDepth: 1"
        "});</script>"
        "</body></html>"
    ).encode()


def route(path):
    """Answer the docs paths. Returns (body_bytes, status) or (None, 404)."""
    name = path.split("?")[0]
    if name == "/openapi.json":
        return openapi_json_bytes(), 200
    if name == "/docs" or name == "/docs/":
        return docs_html(), 200
    return None, 404


def install_openapi_deferred(asgi):
    """Register /openapi.json + /docs as deferred routes on the shared pump."""
    if int(asgi.route_defer("/openapi.json", "application/json")) != 0:
        return -1
    if int(asgi.route_defer("/docs", "text/html; charset=utf-8")) != 0:
        return -1
    return 0
