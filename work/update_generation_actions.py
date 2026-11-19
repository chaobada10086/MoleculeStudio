from pathlib import Path
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8')
start=s.index('    if(b.cmd=="@mechanismNext"){');end=s.index('\n',start)
s=s[:start]+'''    if(b.cmd=="@generationArrowStyle"){generationArrowStyle=b.arg;invalidate(false);return;}
    if(b.cmd=="@mechanismNext"){if(textEditor.active&&!finishText(true))return;generateMechanismStep(0,generationArrowStyle);return;}'''+s[end:]
s=s.replace('generateMechanismStep(b.cmd=="@mechanismTransfer"?1:2);','generateMechanismStep(b.cmd=="@mechanismTransfer"?1:2,generationArrowStyle);')
p.write_text(s,encoding='utf-8')
