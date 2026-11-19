"""Molecule Studio AI drawing enhancement plugin.

Importing this package is side-effect free; the native host declares it in
ai_plugins/plugin.xml and ai_assistant.py calls install_plugin(globals()) so
the stock bridge keeps working when the package is removed.
"""
from .draw_plugin import (
    PLUGIN_MANUAL,
    PLUGIN_NAMES,
    PLUGIN_SYSTEM,
    PLUGIN_TOOLS,
    PLUGIN_VERSION,
    TEMPLATES,
    install_plugin,
)

__all__ = [
    "PLUGIN_MANUAL",
    "PLUGIN_NAMES",
    "PLUGIN_SYSTEM",
    "PLUGIN_TOOLS",
    "PLUGIN_VERSION",
    "TEMPLATES",
    "install_plugin",
]
