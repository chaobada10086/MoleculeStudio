# MoleculeStudio offline chemistry runtime

OPSIN 2.9.0 (MIT): https://github.com/dan2097/opsin
NISPO 0.1.7 (BSD-3-Clause): https://github.com/oxpig/nispo
Ambit-HNMR 1.2 and knowledge base: https://zenodo.org/records/4629851
AMBIT source mirror: https://github.com/ideaconsult/ambit-mirror
Java runtime: Eclipse Adoptium Temurin 21.0.12.1+1; legal notices are in java/legal and java/NOTICE.
RDKit: https://github.com/rdkit/rdkit (provided by the configured local Python).

OPSIN and NISPO licenses are included alongside this file. NISPO also includes its
THIRD_PARTY_NOTICES in python-site/nispo-0.1.7.dist-info/licenses. Original AMBIT JAR
license files are retained in the JAR and copied to licenses/ for convenience.
These are separate upstream components invoked by the MoleculeStudio adapter.

The original hnmr-knowledgebase.txt is unmodified. The derived
hnmr-knowledgebase-moleculestudio.txt changes the alcohol OH pattern from an explicit
H-neighbour to an implicit H count. Unsubstituted benzene uses the upstream 7.26 ppm
baseline in the adapter. Other uncovered H environments are reported as uncovered.
No J constants, solvent-specific models or experimental validation are claimed.

Download URLs, versions and SHA256 hashes: download-manifest.json.
Reconstruction: studio/setup_open_chemistry.py and studio/open_chemistry_dependencies.json.
