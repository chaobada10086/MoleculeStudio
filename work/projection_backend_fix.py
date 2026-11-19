from pathlib import Path
p=Path('studio/chemistry_open.py');s=p.read_text(encoding='utf-8');needle="        edges=list(root.iter('b'))\n        for parsed in mols:";assert needle in s
s=s.replace(needle,"""        edges=list(root.iter('b'))
        def attachment(ident):
            # RDKit replaces a nickname wrapper by its real attachment atom.
            # Resolve that graph identity rather than falling back to coordinates.
            visited=set()
            while ident in nodes and ident not in visited:
                visited.add(ident);fragment=nodes[ident].find('fragment')
                if fragment is None: break
                external=[n.get('id') for n in fragment.findall('n') if n.get('NodeType')=='ExternalConnectionPoint']
                if len(external)!=1: break
                link=next((b for b in fragment.findall('b') if external[0] in (b.get('B'),b.get('E'))),None)
                if link is None: break
                ident=int(link.get('E') if link.get('B')==external[0] else link.get('B'))
            return ident
        for parsed in mols:""")
s=s.replace("                    other=end if begin==ident else begin\n                    if key", "                    other=attachment(end if begin==ident else begin)\n                    if key")
p.write_text(s,encoding='utf-8');Path('out/Studio/chemistry_open.py').write_text(s,encoding='utf-8')
