from pathlib import Path
import json


def engine_sources(base, studio):
    manifest=json.loads((base/'pre_run_manifest.json').read_text(encoding='utf-8'))
    keep={'session.cpp','translate.cpp','native_guard.cpp','ring_geometry.cpp','chemistry_actions.cpp'}
    sources=[s.replace(chr(92),'/') for s in manifest['linked_sources']]
    sources=[s for s in sources if not s.startswith('tests/') and ('/integration/' not in s or Path(s).name in keep)]
    overrides={s:studio/'engine'/Path(s).name for s in sources if (studio/'engine'/Path(s).name).exists()}
    overrides.update({'next/core/commands.cpp':studio/'engine/core_commands.cpp','next/core/document.cpp':studio/'engine/core_document.cpp'})
    overrides.update({'next/arrows_curves/tools.cpp':studio/'engine/arrows_tools.cpp','next/arrows_curves/preset.cpp':studio/'engine/arrows_preset.cpp'})
    overrides['next/arrows_curves/geometry.cpp']=studio/'engine/arrows_geometry.cpp'
    overrides['next/arrows_curves/model.cpp']=studio/'engine/arrows_model.cpp'
    overrides['next/graphics/render_bridge.cpp']=studio/'engine/graphics_render_bridge.cpp'
    overrides['next/render/geometry.cpp']=studio/'engine/render_geometry.cpp'
    overrides['next/bonds/geometry.cpp']=studio/'engine/bonds_geometry.cpp'
    overrides['next/bonds/adapters.cpp']=studio/'engine/bonds_adapters.cpp'
    overrides['next/bonds/spec.cpp']=studio/'engine/bonds_spec.cpp'
    overrides['next/graphics/tables.cpp']=studio/'engine/tables.cpp'
    overrides['next/experimental/tools.cpp']=studio/'engine/experimental_tools.cpp'
    overrides['next/experimental/render_bridge.cpp']=studio/'engine/experimental_render_bridge.cpp'
    return sources, overrides
