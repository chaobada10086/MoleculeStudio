// Offline HOSE lookup. No fetchProton/fetchCarbon or Spinus network calls.
const fs = require('fs');
const path = require('path');
const runtime = path.join(__dirname, 'chemistry_runtime');
const predictorPath = require.resolve(path.join(runtime, 'nmr-js/node_modules/nmr-predictor'));
const predictor = require(predictorPath);
const OCL = require(require.resolve('openchemlib-extended', {paths:[path.dirname(predictorPath)]}));
const nucleus = process.argv[2];
const mol = OCL.Molecule.fromMolfile(fs.readFileSync(process.argv[3], 'utf8'));
const heavy = mol.getAllAtoms();
const carbon = nucleus === '13C';
if (!carbon) mol.addImplicitHydrogens();
mol.ensureHelperArrays(OCL.Molecule.cHelperNeighbours);
const database = JSON.parse(fs.readFileSync(path.join(runtime, carbon ? 'nmrshiftdb2-13c.json' : 'h1.json'), 'utf8'));
// Build the public predictor's JSON input directly. Its old getAllPaths helper
// is incompatible with the currently published floyd-warshall export. Labile
// detection only needs immediate neighbours, not an all-pairs distance matrix.
const diaId = mol.getGroupedDiastereotopicAtomIDs();
for (const group of diaId) {
    group.hose = OCL.Util.getHoseCodesFromDiastereotopicID(group.oclID, {maxSphereSize:5,type:0});
    group.isLabile = group.atomLabel === 'H' && group.atoms.some(i =>
        mol.getConnAtoms(i) && ['O','N','S','Cl'].includes(mol.getAtomLabel(mol.getConnAtom(i,0))));
}
const rows = predictor[carbon ? 'carbon' : 'proton']({diaId}, {db:database, levels:[5,4,3,2], use:'median', ignoreLabile:true});
const result = rows.filter(r=>Number.isFinite(r.delta)).map(r=>{
    const id = Number(r.atomIDs[0]);
    const parent = carbon ? id : (mol.getConnAtoms(id) ? mol.getConnAtom(id,0) : -1);
    if (parent < 0 || parent >= heavy) throw Error('Invalid atom mapping from HOSE engine');
    return {atom:parent,ppm:r.delta,level:r.level,n:r.ncs,std:r.std,min:r.min,max:r.max};
});
process.stdout.write(JSON.stringify(result));
