from pathlib import Path
p=Path('studio/engine/cdxml_reader.cpp');s=p.read_text(encoding='utf-8');s=s.replace('"MoleculeStudioChargeCircle"})','"MoleculeStudioChargeCircle","MoleculeStudioProjection"})');s=s.replace('"MoleculeStudioPerspectiveFront"})','"MoleculeStudioPerspectiveFront","MoleculeStudioProjectionBegin","MoleculeStudioProjectionEnd"})');p.write_text(s,encoding='utf-8')
