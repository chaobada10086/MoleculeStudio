"""Files used for development and diagnostics rather than application runtime."""


# Runtime-only files that are safe to omit from the installed application.
# Scope these rules to known layouts; similarly named files elsewhere may be assets.
TORCH_ROOTS = (
    'chemistry_python/Lib/site-packages/torch/',
    'python/Lib/site-packages/torch/',
    'chemistry_runtime/nmr-ml-site/torch/',
    'ocsr_runtime/venv/Lib/site-packages/torch/',
)


def development_file(parts):
    path = '/'.join(parts).lower()
    for root in TORCH_ROOTS:
        if path.startswith(root.lower()):
            name = path[len(root):]
            # Import/static libraries, C++ headers and CMake files serve extension builds.
            return (name.startswith(('include/', 'share/cmake/'))
                    or (name.startswith('lib/') and name.endswith('.lib')))
    return path in {
        'issue2_fix.md',
        'ai_plugins/scheme-regression-actions.xml',
        'ai_plugins/scheme-regression-result.json',
        'ai_plugins/selftest-actions.xml',
        'ai_plugins/selftest.py',
        'ai_plugins/regression_biomolecules.py',
        'ai_plugins/regression_composition.py',
        'ai_plugins/regression_layout.py',
        'ai_plugins/regression_shapes.py',
    }
