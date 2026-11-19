// =============================================================================
// re_round62_viewer/next/biodraw/preset.cpp
//
// T14 - the 89-row BioDraw preset table. This file is DATA plus the accessors
// over it. There is deliberately no switch over preset ids anywhere in T14: the
// glyph name and the variant index are the whole dispatch, and they are read
// from this table.
//
// The command strings are VERBATIM from tools.csv; the graphic identifiers are
// VERBATIM from the installed GenericShape_Toolbar.xml. See preset.hpp for the
// two independent readings that both produce 89.
// =============================================================================

#include "next/biodraw/preset.hpp"
// params.hpp pulls in preset.hpp (not the other way round), so this is not a
// cycle: preset.hpp stays includable by a consumer that only wants the table,
// while this translation unit needs BioGlyphParams to define the defaults.
#include "next/biodraw/params.hpp"

#include <cstdio>
#include <cstring>
#include <map>

namespace r62 {
namespace biodraw {

// ---------------------------------------------------------------------------
// Section 1 - the table
// ---------------------------------------------------------------------------
// Row order: the 14 families in BioFamily enum order, and inside a family the
// order in which tools.csv lists the commands. So rows of one family are
// CONTIGUOUS (verifyPresetTable() checks this) and presetId == row index.
//
// Reordering rows would change presetId, which IS persisted in the
// AnnotationRecord. That is why the table is append-only: a new preset goes at
// the end of its family block or, if that would renumber, at the very end.
// ---------------------------------------------------------------------------
static const BioPresetSpec kPresets[] = {
    { 0u, "chooseBioTool_1SubstrateEnzyme", "BDC_1_Substrate_Enzyme", "r62.tool.biodraw.1-substrate-enzyme", "1-Substrate Enzyme", BioFamily::Enzyme, GlyphKind::Blob, 0, "Enzyme body with one active-site notch and one substrate" },
    { 1u, "chooseBioTool_2SubstrateEnzyme", "BDC_2_Substrate_Enzyme", "r62.tool.biodraw.2-substrate-enzyme", "2-Substrate Enzyme", BioFamily::Enzyme, GlyphKind::Blob, 1, "Enzyme body with a wider cleft holding two substrates" },
    { 2u, "chooseGenericShape_Enzyme_3", "GS_Enzyme_3", "r62.tool.biodraw.enzyme-3", "Enzyme 3", BioFamily::Enzyme, GlyphKind::Blob, 2, "Two-lobe enzyme body, no substrate" },
    { 3u, "chooseGenericShape_Enzyme_4", "GS_Enzyme_4", "r62.tool.biodraw.enzyme-4", "Enzyme 4", BioFamily::Enzyme, GlyphKind::Blob, 3, "Three-lobe enzyme body" },
    { 4u, "chooseGenericShape_Enzyme_5", "GS_Enzyme_5", "r62.tool.biodraw.enzyme-5", "Enzyme 5", BioFamily::Enzyme, GlyphKind::Blob, 4, "Enzyme body with a central channel" },
    { 5u, "chooseGenericShape_Enzyme_6", "GS_Enzyme_6", "r62.tool.biodraw.enzyme-6", "Enzyme 6", BioFamily::Enzyme, GlyphKind::Blob, 5, "Enzyme body with two bound cofactor beads" },
    { 6u, "chooseGenericShape_Enzyme_7_Kinase_1", "GS_Enzyme_7_Kinase_1", "r62.tool.biodraw.enzyme-7-kinase-1", "Kinase 1", BioFamily::Enzyme, GlyphKind::Blob, 6, "Kinase: two-lobe body with an ATP cleft; editable cleft depth" },
    { 7u, "chooseGenericShape_Enzyme_8_Kinase_2", "GS_Enzyme_8_Kinase_2", "r62.tool.biodraw.enzyme-8-kinase-2", "Kinase 2", BioFamily::Enzyme, GlyphKind::Blob, 7, "Kinase variant with a phosphate bead on the cleft lip" },
    { 8u, "chooseBioTool_GProteinGamma", "BDC_G_Protein_GammaSubunit", "r62.tool.biodraw.gprotein-gamma", "G-Protein Gamma", BioFamily::GProtein, GlyphKind::Blob, 8, "Gamma subunit, small single-lobe body" },
    { 9u, "chooseBioTool_GProteinBeta", "BDC_G_Protein_BetaSubunit", "r62.tool.biodraw.gprotein-beta", "G-Protein Beta", BioFamily::GProtein, GlyphKind::Blob, 9, "Beta subunit, propeller body with subunit beads" },
    { 10u, "chooseBioTool_GProteinAlpha", "BDC_G_Protein_AlphaSubunit", "r62.tool.biodraw.gprotein-alpha", "G-Protein Alpha", BioFamily::GProtein, GlyphKind::Blob, 10, "Alpha subunit with a bound GTP bead; bead count editable" },
    { 11u, "chooseGenericShape_GProtein_4", "GS_G_Protein_4", "r62.tool.biodraw.gprotein-4", "G-Protein 4", BioFamily::GProtein, GlyphKind::Blob, 11, "Alpha+beta+gamma trimer arrangement" },
    { 12u, "chooseGenericShape_GProtein_5", "GS_G_Protein_5", "r62.tool.biodraw.gprotein-5", "G-Protein 5", BioFamily::GProtein, GlyphKind::Blob, 12, "Trimer plus effector contact, extra subunit bead" },
    { 13u, "chooseGenericShape_GProtein_6", "GS_G_Protein_6", "r62.tool.biodraw.gprotein-6", "G-Protein 6", BioFamily::GProtein, GlyphKind::Blob, 13, "Dissociated alpha with receptor contact" },
    { 14u, "chooseBioTool_Receptor", "BDC_Receptor", "r62.tool.biodraw.receptor", "Receptor", BioFamily::Receptor, GlyphKind::Receptor, 0, "7-TM serpentine; tmSegments editable, ligand optional" },
    { 15u, "chooseGenericShape_Receptor_2", "GS_Receptor_2", "r62.tool.biodraw.receptor-2", "Receptor 2", BioFamily::Receptor, GlyphKind::Receptor, 1, "5-TM serpentine, no ligand" },
    { 16u, "chooseGenericShape_Receptor_3", "GS_Receptor_3", "r62.tool.biodraw.receptor-3", "Receptor 3", BioFamily::Receptor, GlyphKind::Receptor, 2, "7-TM with bound ligand; ligandPresent editable" },
    { 17u, "chooseGenericShape_Receptor_4", "GS_Receptor_4", "r62.tool.biodraw.receptor-4", "Receptor 4", BioFamily::Receptor, GlyphKind::Receptor, 3, "7-TM with a G-protein contact at the C terminus" },
    { 18u, "chooseGenericShape_Receptor_5", "GS_Receptor_5", "r62.tool.biodraw.receptor-5", "Receptor 5", BioFamily::Receptor, GlyphKind::Receptor, 4, "Single-pass receptor with an extracellular domain" },
    { 19u, "chooseGenericShape_Receptor_6", "GS_Receptor_6", "r62.tool.biodraw.receptor-6", "Receptor 6", BioFamily::Receptor, GlyphKind::Receptor, 5, "Ionotropic receptor: 5-subunit ring around a pore" },
    { 20u, "chooseGenericShape_Receptor_7", "GS_Receptor_7", "r62.tool.biodraw.receptor-7", "Receptor 7", BioFamily::Receptor, GlyphKind::Receptor, 6, "Tyrosine-kinase dimer, two single-pass chains" },
    { 21u, "chooseGenericShape_Receptor_8", "GS_Receptor_8", "r62.tool.biodraw.receptor-8", "Receptor 8", BioFamily::Receptor, GlyphKind::Receptor, 7, "Nuclear receptor with a DNA-binding domain" },
    { 22u, "chooseGenericShape_Receptor_9", "GS_Receptor_9", "r62.tool.biodraw.receptor-9", "Receptor 9", BioFamily::Receptor, GlyphKind::Receptor, 8, "GPCR with a second messenger bead" },
    { 23u, "chooseGenericShape_Receptor_10", "GS_Receptor_10", "r62.tool.biodraw.receptor-10", "Receptor 10", BioFamily::Receptor, GlyphKind::Receptor, 9, "7-TM with an intracellular scaffold bead" },
    { 24u, "chooseGenericShape_Receptor_11", "GS_Receptor_11", "r62.tool.biodraw.receptor-11", "Receptor 11", BioFamily::Receptor, GlyphKind::Receptor, 10, "Receptor with a lipid-anchored C terminus; anchor editable" },
    { 25u, "chooseBioTool_IonChannel", "BDC_IonChannel", "r62.tool.biodraw.ion-channel", "Ion Channel", BioFamily::IonChannel, GlyphKind::Channel, 0, "Membrane gate with a selectivity filter; tmSegments editable" },
    { 26u, "chooseGenericShape_IonChannel_2", "GS_IonChannel_2", "r62.tool.biodraw.ion-channel-2", "Ion Channel 2", BioFamily::IonChannel, GlyphKind::Channel, 1, "Voltage-gated: 4 TM segments per subunit" },
    { 27u, "chooseGenericShape_IonChannel_3", "GS_IonChannel_3", "r62.tool.biodraw.ion-channel-3", "Ion Channel 3", BioFamily::IonChannel, GlyphKind::Channel, 2, "Ligand-gated channel with a bound ligand" },
    { 28u, "chooseGenericShape_IonChannel_4", "GS_IonChannel_4", "r62.tool.biodraw.ion-channel-4", "Ion Channel 4", BioFamily::IonChannel, GlyphKind::Channel, 3, "Two-pore channel" },
    { 29u, "chooseGenericShape_IonChannel_5", "GS_IonChannel_5", "r62.tool.biodraw.ion-channel-5", "Ion Channel 5", BioFamily::IonChannel, GlyphKind::Channel, 4, "Mechanosensitive channel, wide pore" },
    { 30u, "chooseGenericShape_IonChannel_6", "GS_IonChannel_6", "r62.tool.biodraw.ion-channel-6", "Ion Channel 6", BioFamily::IonChannel, GlyphKind::Channel, 5, "Channel with a disabled/open gate; gate angle editable" },
    { 31u, "chooseGenericShape_IonChannel_7", "GS_IonChannel_7", "r62.tool.biodraw.ion-channel-7", "Ion Channel 7", BioFamily::IonChannel, GlyphKind::Channel, 6, "Aquaporin-style tetramer" },
    { 32u, "chooseGenericShape_IonChannel_8", "GS_IonChannel_8", "r62.tool.biodraw.ion-channel-8", "Ion Channel 8", BioFamily::IonChannel, GlyphKind::Channel, 7, "Channel with two ions in the filter" },
    { 33u, "chooseGenericShape_IonChannel_9", "GS_IonChannel_9", "r62.tool.biodraw.ion-channel-9", "Ion Channel 9", BioFamily::IonChannel, GlyphKind::Channel, 8, "Channel with an auxiliary beta subunit" },
    { 34u, "chooseGenericShape_IonChannel_10", "GS_IonChannel_10", "r62.tool.biodraw.ion-channel-10", "Ion Channel 10", BioFamily::IonChannel, GlyphKind::Channel, 9, "Channel plus a regulatory bead on the cytosolic side" },
    { 35u, "chooseGenericShape_IonChannel_11", "GS_IonChannel_11", "r62.tool.biodraw.ion-channel-11", "Ion Channel 11", BioFamily::IonChannel, GlyphKind::Channel, 10, "Channel in a bilayer; lipids are a component" },
    { 36u, "chooseGenericShape_IonChannel_12", "GS_IonChannel_12", "r62.tool.biodraw.ion-channel-12", "Ion Channel 12", BioFamily::IonChannel, GlyphKind::Channel, 11, "Channel with a plug/ball on the cytosolic mouth" },
    { 37u, "chooseGenericShape_IonChannel_13", "GS_IonChannel_13", "r62.tool.biodraw.ion-channel-13", "Ion Channel 13", BioFamily::IonChannel, GlyphKind::Channel, 12, "Channel with a toxin bead bound in the pore" },
    { 38u, "chooseBioTool_Immunoglobin", "BDC_Immunoglobin", "r62.tool.biodraw.immunoglobin", "Immunoglobin", BioFamily::Immunoglobin, GlyphKind::Antibody, 0, "Y antibody: 2 heavy + 2 light chains; disulfides editable" },
    { 39u, "chooseGenericShape_Immunoglobin_2", "GS_Immunoglobin_2", "r62.tool.biodraw.immunoglobin-2", "Immunoglobin 2", BioFamily::Immunoglobin, GlyphKind::Antibody, 1, "Fab fragment only, one arm" },
    { 40u, "chooseGenericShape_Immunoglobin_3", "GS_Immunoglobin_3", "r62.tool.biodraw.immunoglobin-3", "Immunoglobin 3", BioFamily::Immunoglobin, GlyphKind::Antibody, 2, "Fc fragment, two chains" },
    { 41u, "chooseGenericShape_Immunoglobin_4", "GS_Immunoglobin_4", "r62.tool.biodraw.immunoglobin-4", "Immunoglobin 4", BioFamily::Immunoglobin, GlyphKind::Antibody, 3, "Antibody with an antigen bead at each Fab tip" },
    { 42u, "chooseGenericShape_Immunoglobin_5", "GS_Immunoglobin_5", "r62.tool.biodraw.immunoglobin-5", "Immunoglobin 5", BioFamily::Immunoglobin, GlyphKind::Antibody, 4, "Single-chain variable fragment (scFv), one linker" },
    { 43u, "chooseGenericShape_NuclearPore_1", "GS_NuclearPore_1", "r62.tool.biodraw.nuclear-pore-1", "Nuclear Pore 1", BioFamily::NuclearPore, GlyphKind::Pore, 0, "Ring of pore subunits around a membrane disc" },
    { 44u, "chooseGenericShape_NuclearPore_2", "GS_NuclearPore_2", "r62.tool.biodraw.nuclear-pore-2", "Nuclear Pore 2", BioFamily::NuclearPore, GlyphKind::Pore, 1, "Pore with a central transporter and basket fibrils" },
    { 45u, "chooseBioTool_DNA", "BD_DNA", "r62.tool.biodraw.dna", "DNA", BioFamily::NucleicAcid, GlyphKind::DoubleHelix, 0, "SCHEMATIC double helix; basePairs, radius and rise are editable" },
    { 46u, "chooseBioTool_tRNA", "BDC_tRNA_Cloverleaf", "r62.tool.biodraw.t-rna", "tRNA", BioFamily::NucleicAcid, GlyphKind::Cloverleaf, 0, "Cloverleaf secondary structure; loop bead counts editable" },
    { 47u, "chooseGenericShape_mRNA", "GS_mRNA", "r62.tool.biodraw.m-rna", "mRNA", BioFamily::NucleicAcid, GlyphKind::Strand, 0, "Single ribbon with codon beads; bead count editable" },
    { 48u, "chooseBioTool_MembraneLine", "BDC_MembraneLine", "r62.tool.biodraw.membrane-line", "Membrane Line", BioFamily::Membrane, GlyphKind::Bilayer, 0, "Straight bilayer; lipid count and spacing editable" },
    { 49u, "chooseBioTool_MembraneArc", "BDC_MembraneArc", "r62.tool.biodraw.membrane-arc", "Membrane Arc", BioFamily::Membrane, GlyphKind::Bilayer, 1, "Curved bilayer; sweep angle editable" },
    { 50u, "chooseBioTool_Membrane_Ellipse", "BDC_MembraneEllipse", "r62.tool.biodraw.membrane-ellipse", "Membrane Ellipse", BioFamily::Membrane, GlyphKind::Vesicle, 0, "Closed elliptical bilayer ring" },
    { 51u, "chooseBioTool_Membrane_Micelle", "BDC_Micelle", "r62.tool.biodraw.membrane-micelle", "Micelle", BioFamily::Membrane, GlyphKind::Vesicle, 1, "Micelle with a tail core; lipid count editable" },
    { 52u, "chooseBioTool_Mitochondrion", "BDC_Mitocondrion", "r62.tool.biodraw.mitochondrion", "Mitochondrion", BioFamily::Organelle, GlyphKind::Capsule, 0, "Capsule with inner cristae; crista count editable" },
    { 53u, "chooseGenericShape_Mitochondrion_2", "GS_Mitocondrion_2", "r62.tool.biodraw.mitochondrion-2", "Mitochondrion 2", BioFamily::Organelle, GlyphKind::Capsule, 1, "Mitochondrion with a second outer membrane and ribosome beads" },
    { 54u, "chooseBioTool_Golgi", "BDC_GolgiBody", "r62.tool.biodraw.golgi", "Golgi Body", BioFamily::Organelle, GlyphKind::Tube, 0, "Stacked cisternae; cisterna count editable (shared with Golgi toolbar entry)" },
    { 55u, "chooseGenericShape_Golgi_2", "GS_Golgi_2", "r62.tool.biodraw.golgi-2", "Golgi 2", BioFamily::Organelle, GlyphKind::Tube, 1, "Golgi with vesicles budding from the trans face" },
    { 56u, "chooseGenericShape_Golgi_3", "GS_Golgi_3", "r62.tool.biodraw.golgi-3", "Golgi 3", BioFamily::Organelle, GlyphKind::Tube, 2, "Golgi with a wider cisternal gap" },
    { 57u, "chooseGenericShape_Golgi_4", "GS_Golgi_4", "r62.tool.biodraw.golgi-4", "Golgi 4", BioFamily::Organelle, GlyphKind::Tube, 3, "Golgi with two budding vesicle clusters" },
    { 58u, "chooseBioTool_Er", "BDC_EndoplasmicReticulum", "r62.tool.biodraw.er", "Endoplasmic Reticulum", BioFamily::Organelle, GlyphKind::Spiralling, 0, "Rough ER ribbon with ribosome beads (shared with ER toolbar entry)" },
    { 59u, "chooseGenericShape_Centriole", "GS_Centriole", "r62.tool.biodraw.centriole", "Centriole", BioFamily::Organelle, GlyphKind::Barrel, 0, "9-triplet barrel; triplet count editable" },
    { 60u, "chooseGenericShape_ER_2", "GS_ER_2", "r62.tool.biodraw.er-2", "ER 2", BioFamily::Organelle, GlyphKind::Spiralling, 1, "Smooth ER, fewer turns" },
    { 61u, "chooseGenericShape_ER_3", "GS_ER_3", "r62.tool.biodraw.er-3", "ER 3", BioFamily::Organelle, GlyphKind::Spiralling, 2, "ER with a wider lumen gap" },
    { 62u, "chooseGenericShape_ER_4", "GS_ER_4", "r62.tool.biodraw.er-4", "ER 4", BioFamily::Organelle, GlyphKind::Spiralling, 3, "ER with a nuclear-envelope contact" },
    { 63u, "chooseGenericShape_ER_5", "GS_ER_5", "r62.tool.biodraw.er-5", "ER 5", BioFamily::Organelle, GlyphKind::Spiralling, 4, "ER network: two connected ribbons" },
    { 64u, "chooseBioTool_RibosomeA", "BDC_RibosomeA", "r62.tool.biodraw.ribosome-a", "Ribosome A", BioFamily::Ribosome, GlyphKind::Ribosome, 0, "Large + small subunit, 50S/30S proportions" },
    { 65u, "chooseBioTool_RibosomeB", "BDC_RibosomeB", "r62.tool.biodraw.ribosome-b", "Ribosome B", BioFamily::Ribosome, GlyphKind::Ribosome, 1, "Ribosome with an mRNA strand threaded through" },
    { 66u, "chooseGenericShape_Ribosome_3", "GS_Ribosome_3", "r62.tool.biodraw.ribosome-3", "Ribosome 3", BioFamily::Ribosome, GlyphKind::Ribosome, 2, "Polysome: several ribosomes on one strand; bead count editable" },
    { 67u, "chooseGenericShape_Ribosome_4", "GS_Ribosome_4", "r62.tool.biodraw.ribosome-4", "Ribosome 4", BioFamily::Ribosome, GlyphKind::Ribosome, 3, "Ribosome with a nascent peptide chain" },
    { 68u, "chooseGenericShape_Ribosome_5", "GS_Ribosome_5", "r62.tool.biodraw.ribosome-5", "Ribosome 5", BioFamily::Ribosome, GlyphKind::Ribosome, 4, "Single large subunit only" },
    { 69u, "chooseGenericShape_Ribosome_6", "GS_Ribosome_6", "r62.tool.biodraw.ribosome-6", "Ribosome 6", BioFamily::Ribosome, GlyphKind::Ribosome, 5, "Ribosome with tRNA beads in the A/P/E sites" },
    { 70u, "chooseBioTool_Cloud", "BDC_Cloud", "r62.tool.biodraw.cloud", "Cloud", BioFamily::Misc, GlyphKind::Cloud, 0, "Scalloped cloud; scallop count editable" },
    { 71u, "chooseGenericShape_Star", "GS_Star", "r62.tool.biodraw.star", "Star", BioFamily::Misc, GlyphKind::Star, 0, "N-point star; point count and inner ratio editable" },
    { 72u, "chooseGenericShape_Circle", "GS_Circle", "r62.tool.biodraw.circle", "Circle", BioFamily::Misc, GlyphKind::Polygon, 0, "Circle as a 64-gon; radius indiexible, node count editable" },
    { 73u, "chooseGenericShape_Ball", "GS_Ball", "r62.tool.biodraw.ball", "Ball", BioFamily::Misc, GlyphKind::Polygon, 1, "Ball with a highlight lobe" },
    { 74u, "chooseGenericShape_Triangle", "GS_Triangle", "r62.tool.biodraw.triangle", "Triangle", BioFamily::Misc, GlyphKind::Polygon, 2, "Regular 3-gon" },
    { 75u, "chooseGenericShape_Rectangle", "GS_Rectangle", "r62.tool.biodraw.rectangle", "Rectangle", BioFamily::Misc, GlyphKind::Polygon, 3, "Axis-aligned rectangle; corner fraction 0" },
    { 76u, "chooseGenericShape_Hexagon", "GS_Hexagon", "r62.tool.biodraw.hexagon", "Hexagon", BioFamily::Misc, GlyphKind::Polygon, 4, "Regular 6-gon" },
    { 77u, "chooseGenericShape_Octagon", "GS_Octagon", "r62.tool.biodraw.octagon", "Octagon", BioFamily::Misc, GlyphKind::Polygon, 5, "Regular 8-gon" },
    { 78u, "chooseGenericShape_Virus", "GS_Virus", "r62.tool.biodraw.virus", "Virus", BioFamily::Microorganism, GlyphKind::Virus, 0, "Capsid polygon with surface spikes; spikes editable" },
    { 79u, "chooseGenericShape_Virus2", "GS_Virus2", "r62.tool.biodraw.virus2", "Virus 2", BioFamily::Microorganism, GlyphKind::Virus, 1, "Enveloped virus: capsid inside a bilayer envelope" },
    { 80u, "chooseGenericShape_Virus3", "GS_Virus3", "r62.tool.biodraw.virus3", "Virus 3", BioFamily::Microorganism, GlyphKind::Virus, 2, "Filamentous virus, no polygonal capsid" },
    { 81u, "chooseGenericShape_HIV", "GS_HIV", "r62.tool.biodraw.hiv", "HIV", BioFamily::Microorganism, GlyphKind::Virus, 3, "Enveloped capsid with gp120 spikes and a cone core" },
    { 82u, "chooseGenericShape_Bacterium", "GS_Bacterium", "r62.tool.biodraw.bacterium", "Bacterium", BioFamily::Microorganism, GlyphKind::Capsule, 2, "Rod cell with an optional flagellum" },
    { 83u, "chooseGenericShape_Bacterium2", "GS_Bacterium2", "r62.tool.biodraw.bacterium2", "Bacterium 2", BioFamily::Microorganism, GlyphKind::Capsule, 3, "Coccus cell with a capsule layer" },
    { 84u, "chooseGenericShape_Bacteriophage", "GS_Bacteriophage", "r62.tool.biodraw.bacteriophage", "Bacteriophage", BioFamily::Microorganism, GlyphKind::Bacteriophage, 0, "Icosahedral head, tail and fibres; fibre count editable" },
    { 85u, "chooseGenericShape_Paramecium", "GS_Paramecium", "r62.tool.biodraw.paramecium", "Paramecium", BioFamily::Microorganism, GlyphKind::Blob, 14, "Slipper body with cilia; cilia count editable" },
    { 86u, "chooseGenericShape_Amoeba", "GS_Amoeba", "r62.tool.biodraw.amoeba", "Amoeba", BioFamily::Microorganism, GlyphKind::Blob, 15, "Irregular body with pseudopods; pseudopod count editable" },
    { 87u, "chooseBioTool_HelixProtein", "BDC_HelixProtein", "r62.tool.biodraw.helix-protein", "Helix Protein", BioFamily::HelixProtein, GlyphKind::Spiralling, 5, "Coiled helix; turns and amplitude editable" },
    { 88u, "chooseBioTool_PlasmidMap", "BDC_PlasmidMap", "r62.tool.biodraw.plasmid-map", "Plasmid Map", BioFamily::PlasmidMap, GlyphKind::Plasmid, 0, "Circular base-range map; markers/regions are editable data" },
};

static const size_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

const int kFamilyExpectedCount[static_cast<int>(BioFamily::Count)] = {
    8, 6, 11, 13, 5, 2, 3, 4, 12, 6, 8, 9, 1, 1
};
// ---------------------------------------------------------------------------
// Section 2 - family / glyph / component names
// ---------------------------------------------------------------------------
const char* familyName(BioFamily f) {
    switch (f) {
        case BioFamily::Enzyme:        return "Enzyme";
        case BioFamily::GProtein:      return "GProtein";
        case BioFamily::Receptor:      return "Receptor";
        case BioFamily::IonChannel:    return "IonChannel";
        case BioFamily::Immunoglobin:  return "Immunoglobin";
        case BioFamily::NuclearPore:   return "NuclearPore";
        case BioFamily::NucleicAcid:   return "NucleicAcid";
        case BioFamily::Membrane:      return "Membrane";
        case BioFamily::Organelle:     return "Organelle";
        case BioFamily::Ribosome:      return "Ribosome";
        case BioFamily::Misc:          return "Misc";
        case BioFamily::Microorganism: return "Microorganism";
        case BioFamily::HelixProtein:  return "HelixProtein";
        case BioFamily::PlasmidMap:    return "PlasmidMap";
        case BioFamily::Count:         break;
    }
    return "Unknown";
}

bool familyFromName(const std::string& name, BioFamily* out) {
    if (!out) return false;
    for (int i = 0; i < static_cast<int>(BioFamily::Count); ++i) {
        if (name == familyName(static_cast<BioFamily>(i))) {
            *out = static_cast<BioFamily>(i);
            return true;
        }
    }
    return false;
}

int familyIndex(BioFamily f) { return static_cast<int>(f); }

const char* glyphName(GlyphKind g) {
    switch (g) {
        case GlyphKind::Blob:          return "Blob";
        case GlyphKind::Bilayer:       return "Bilayer";
        case GlyphKind::Vesicle:       return "Vesicle";
        case GlyphKind::Capsule:       return "Capsule";
        case GlyphKind::Rod:           return "Rod";
        case GlyphKind::Spiralling:    return "Spiralling";
        case GlyphKind::Tube:          return "Tube";
        case GlyphKind::Pore:          return "Pore";
        case GlyphKind::Barrel:        return "Barrel";
        case GlyphKind::BarrelPore:    return "BarrelPore";
        case GlyphKind::YShape:        return "YShape";
        case GlyphKind::Antibody:      return "Antibody";
        case GlyphKind::Receptor:      return "Receptor";
        case GlyphKind::Channel:       return "Channel";
        case GlyphKind::DoubleHelix:   return "DoubleHelix";
        case GlyphKind::Cloverleaf:    return "Cloverleaf";
        case GlyphKind::Strand:        return "Strand";
        case GlyphKind::Ribosome:      return "Ribosome";
        case GlyphKind::Star:          return "Star";
        case GlyphKind::Polygon:       return "Polygon";
        case GlyphKind::Cloud:         return "Cloud";
        case GlyphKind::Virus:         return "Virus";
        case GlyphKind::Bacteriophage: return "Bacteriophage";
        case GlyphKind::Plasmid:       return "Plasmid";
        case GlyphKind::Count:         break;
    }
    return "Unknown";
}

const char* componentName(ComponentKind k) {
    switch (k) {
        case ComponentKind::None:          return "None";
        case ComponentKind::LipidHead:     return "LipidHead";
        case ComponentKind::LipidTail:     return "LipidTail";
        case ComponentKind::Subunit:       return "Subunit";
        case ComponentKind::Transmembrane: return "Transmembrane";
        case ComponentKind::BasePair:      return "BasePair";
        case ComponentKind::Nucleotide:    return "Nucleotide";
        case ComponentKind::Bead:          return "Bead";
        case ComponentKind::Spike:         return "Spike";
        case ComponentKind::Crista:        return "Crista";
        case ComponentKind::Cisterna:      return "Cisterna";
        case ComponentKind::PoreSubunit:   return "PoreSubunit";
        case ComponentKind::Flagellum:     return "Flagellum";
        case ComponentKind::Marker:        return "Marker";
        case ComponentKind::Region:        return "Region";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Section 3 - accessors
// ---------------------------------------------------------------------------
const BioPresetSpec* bioPresets()      { return kPresets; }
size_t               bioPresetCount()  { return kPresetCount; }

const BioPresetSpec* bioPresetById(uint32_t id) {
    return id < kPresetCount ? &kPresets[id] : 0;
}

const BioPresetSpec* bioPresetByCommand(const std::string& command) {
    for (size_t i = 0; i < kPresetCount; ++i)
        if (command == kPresets[i].sourceCommand) return &kPresets[i];
    return 0;
}

const BioPresetSpec* bioPresetByGraphic(const std::string& graphic) {
    for (size_t i = 0; i < kPresetCount; ++i)
        if (graphic == kPresets[i].graphic) return &kPresets[i];
    return 0;
}

const BioPresetSpec* bioPresetByToolId(const std::string& toolId) {
    for (size_t i = 0; i < kPresetCount; ++i)
        if (toolId == kPresets[i].toolId) return &kPresets[i];
    return 0;
}

const BioPresetSpec* bioPresetByFamilyIndex(BioFamily family, int index) {
    if (index < 0) return 0;
    int seen = 0;
    for (size_t i = 0; i < kPresetCount; ++i) {
        if (kPresets[i].family != family) continue;
        if (seen == index) return &kPresets[i];
        ++seen;
    }
    return 0;
}

size_t bioPresetCountInFamily(BioFamily family) {
    size_t n = 0;
    for (size_t i = 0; i < kPresetCount; ++i) if (kPresets[i].family == family) ++n;
    return n;
}
// ---------------------------------------------------------------------------
// Section 4 - table integrity (checked at runtime, not asserted in a comment)
// ---------------------------------------------------------------------------
bool PresetTableAudit::ok() const {
    return rowCount == 89 && distinctCommands == 89 && distinctGraphics == 89 &&
           distinctToolIds == 89 && familyMismatches == 0 && duplicateReceipts == 0 &&
           tableOrderIsIdStable && familyBlocksContiguous;
}

std::string PresetTableAudit::summary() const {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer),
                  "rows=%d commands=%d graphics=%d toolIds=%d dupReceipts=%d"
                  " familyMismatches=%d idStable=%d contiguous=%d",
                  rowCount, distinctCommands, distinctGraphics, distinctToolIds,
                  duplicateReceipts, familyMismatches,
                  tableOrderIsIdStable ? 1 : 0, familyBlocksContiguous ? 1 : 0);
    return std::string(buffer);
}

PresetTableAudit verifyPresetTable() {
    PresetTableAudit audit;
    std::memset(&audit, 0, sizeof(audit));
    audit.rowCount = static_cast<int>(kPresetCount);
    // These two are "true unless proven false", so they must START true. memset()
    // zeroed them, and a flag that is only ever assigned false can never pass --
    // which is a defect in the AUDIT rather than in the table it checks. The first
    // smoke run reported idStable=0 for a table whose ids were perfectly stable,
    // and that is how this was found.
    audit.tableOrderIsIdStable = true;

    std::map<std::string, int> commandSeen, graphicSeen, toolIdSeen;
    for (size_t i = 0; i < kPresetCount; ++i) {
        const BioPresetSpec& p = kPresets[i];
        if (commandSeen.count(p.sourceCommand)) ++audit.duplicateReceipts;
        commandSeen[p.sourceCommand] = 1;
        graphicSeen[p.graphic] = 1;
        toolIdSeen[p.toolId] = 1;
        ++audit.familyCounts[static_cast<int>(p.family)];
        if (p.presetId != static_cast<uint32_t>(i)) audit.tableOrderIsIdStable = false;
    }
    audit.distinctCommands = static_cast<int>(commandSeen.size());
    audit.distinctGraphics = static_cast<int>(graphicSeen.size());
    audit.distinctToolIds  = static_cast<int>(toolIdSeen.size());

    for (int f = 0; f < static_cast<int>(BioFamily::Count); ++f) {
        if (audit.familyCounts[f] != kFamilyExpectedCount[f]) ++audit.familyMismatches;
    }

    // A family block is contiguous when, after the first row of a family, no
    // row of a DIFFERENT family appears and then the family appears again.
    audit.familyBlocksContiguous = true;
    bool closed[static_cast<int>(BioFamily::Count)];
    for (int f = 0; f < static_cast<int>(BioFamily::Count); ++f) closed[f] = false;
    int previous = -1;
    for (size_t i = 0; i < kPresetCount; ++i) {
        const int f = static_cast<int>(kPresets[i].family);
        if (f != previous) {
            if (previous >= 0) closed[previous] = true;
            if (closed[f]) audit.familyBlocksContiguous = false;
            previous = f;
        }
    }
    // Every family except the last must actually appear.
    for (int f = 0; f < static_cast<int>(BioFamily::Count); ++f)
        if (audit.familyCounts[f] == 0) audit.familyBlocksContiguous = false;

    return audit;
}
// ---------------------------------------------------------------------------
// Section 5 - per-preset default components and glyph numbers
// ---------------------------------------------------------------------------
// THESE ARE THE DEFAULTS A USER THEN EDITS. They are [PROJ] values: the vendor
// XML gives an icon name and nothing about subunit counts, so the numbers below
// are this project's choice and are stated as such. They are NOT claimed to be
// ChemDraw's internal constants.
//
// The mapping is keyed on the GLYPH and the VARIANT, not on the preset id, so a
// new preset of an existing glyph gets sensible defaults for free.
namespace {

ComponentSpec makeSpec(ComponentKind kind, int count, double spacing, double unitSize) {
    ComponentSpec spec;
    spec.kind = kind;
    spec.count = count;
    spec.spacing = spacing;
    spec.unitSize = unitSize;
    spec.phase = 0.0;
    spec.evenlySpaced = true;
    return spec;
}

} // namespace

std::vector<ComponentSpec> defaultComponents(const BioPresetSpec& spec) {
    std::vector<ComponentSpec> out;
    switch (spec.glyph) {
        case GlyphKind::Blob:
            // Bound beads: substrates, cofactors, cilia, pseudopods. The variant
            // decides how many, which is the ONLY difference between several
            // enzyme and G-protein presets.
            switch (spec.variant) {
                case 0:  out.push_back(makeSpec(ComponentKind::Bead, 1, 8.0, 2.2)); break;
                case 1:  out.push_back(makeSpec(ComponentKind::Bead, 2, 8.0, 2.2)); break;
                case 5:  out.push_back(makeSpec(ComponentKind::Bead, 2, 7.0, 1.8)); break;
                case 7:  out.push_back(makeSpec(ComponentKind::Bead, 1, 8.0, 2.0)); break;
                case 9:  out.push_back(makeSpec(ComponentKind::Bead, 4, 9.0, 2.0)); break;
                case 10: out.push_back(makeSpec(ComponentKind::Bead, 1, 8.0, 2.4)); break;
                case 11: out.push_back(makeSpec(ComponentKind::Bead, 3, 10.0, 2.6)); break;
                case 12: out.push_back(makeSpec(ComponentKind::Bead, 4, 10.0, 2.6)); break;
                case 13: out.push_back(makeSpec(ComponentKind::Bead, 1, 8.0, 2.4)); break;
                case 14: out.push_back(makeSpec(ComponentKind::Bead, 12, 4.0, 0.9)); break;
                case 15: out.push_back(makeSpec(ComponentKind::Bead, 5, 9.0, 2.6)); break;
                default: break;
            }
            break;
        case GlyphKind::Bilayer:
            out.push_back(makeSpec(ComponentKind::LipidHead, 17, 5.0, 1.2));
            break;
        case GlyphKind::Vesicle:
            out.push_back(makeSpec(ComponentKind::LipidHead,
                                   spec.variant == 1 ? 24 : 32, 4.0, 1.2));
            break;
        case GlyphKind::Capsule:
            // Cristae first, then beads/surface proteins, then the nucleoid path.
            out.push_back(makeSpec(ComponentKind::Crista, 7, 6.0, 1.0));
            out.push_back(makeSpec(ComponentKind::Bead, spec.variant == 1 ? 6 : 0, 7.0, 1.4));
            out.push_back(makeSpec(ComponentKind::None, 0, 6.0, 1.0));
            break;
        case GlyphKind::Rod:
            out.push_back(makeSpec(ComponentKind::Bead, 4, 8.0, 1.4));
            break;
        case GlyphKind::Spiralling:
            if (spec.variant == 0 || spec.variant == 1)
                out.push_back(makeSpec(ComponentKind::Bead, 8, 6.0, 1.1));   // rough ER
            break;
        case GlyphKind::Tube:
            out.push_back(makeSpec(ComponentKind::Cisterna, spec.variant + 3, 7.0, 1.0));
            out.push_back(makeSpec(ComponentKind::Bead,
                                   spec.variant == 1 || spec.variant == 3 ? 5 : 0, 7.0, 1.6));
            break;
        case GlyphKind::Pore:
            out.push_back(makeSpec(ComponentKind::PoreSubunit, 8, 6.0, 1.8));
            break;
        case GlyphKind::Barrel:
            out.push_back(makeSpec(ComponentKind::PoreSubunit, 9, 5.0, 1.5));   // 9 triplets
            break;
        case GlyphKind::BarrelPore:
            out.push_back(makeSpec(ComponentKind::Bead, 2, 6.0, 1.4));
            break;
        case GlyphKind::YShape:
        case GlyphKind::Antibody:
            // Antigen beads at the Fab tips, except for Fc-only (variant 2).
            out.push_back(makeSpec(ComponentKind::Bead, spec.variant == 2 ? 0 : 2, 9.0, 2.0));
            break;
        case GlyphKind::Receptor:
            // Bound beads: ligand, G-protein contact, scaffold, messenger.
            out.push_back(makeSpec(ComponentKind::Bead,
                                   (spec.variant == 3 || spec.variant == 8 ||
                                    spec.variant == 9 || spec.variant == 10) ? 1 : 0,
                                   9.0, 2.0));
            break;
        case GlyphKind::Channel:
            // Ions in the filter, auxiliary subunit, toxin, plug.
            out.push_back(makeSpec(ComponentKind::Bead,
                                   (spec.variant == 7) ? 2 : (spec.variant == 12 ? 1 : 0),
                                   6.0, 1.5));
            break;
        case GlyphKind::DoubleHelix:
            // The rungs ARE the base-pair count.
            out.push_back(makeSpec(ComponentKind::BasePair, 12, 3.0, 1.0));
            break;
        case GlyphKind::Cloverleaf:
            out.push_back(makeSpec(ComponentKind::Nucleotide, 12, 3.0, 0.9));
            break;
        case GlyphKind::Strand:
            out.push_back(makeSpec(ComponentKind::Nucleotide, 14, 4.0, 1.0));   // codon beads
            break;
        case GlyphKind::Ribosome:
            out.push_back(makeSpec(ComponentKind::Bead,
                                   spec.variant == 2 ? 4 : (spec.variant == 5 ? 3 : 0),
                                   8.0, 1.8));
            break;
        case GlyphKind::Virus:
            out.push_back(makeSpec(ComponentKind::Spike, spec.variant == 1 ? 12 : 10, 5.0, 1.6));
            break;
        case GlyphKind::Bacteriophage:
            out.push_back(makeSpec(ComponentKind::Spike, 6, 5.0, 1.4));   // tail fibres
            break;
        case GlyphKind::Plasmid:
            // Base ticks: one every ~100 bp is the conventional map density, so
            // the tick COUNT is derived from the base count rather than fixed.
            out.push_back(makeSpec(ComponentKind::Marker, 0, 5.0, 0.5));
            break;
        default:
            break;
    }
    return out;
}

void defaultGlyphParamsFor(const BioPresetSpec& spec, BioGlyphParams* out) {
    if (!out) return;
    BioGlyphParams& g = *out;
    switch (spec.glyph) {
        case GlyphKind::Blob:
            g.lobeDepth = 0.15;
            if (spec.variant == 14 || spec.variant == 15) {
                g.lobes = 5.0;
                g.lobeDepth = 0.22;
            }
            break;
        case GlyphKind::Bilayer:
            g.leafletGap = 3.0;
            g.headRadius = 1.2;
            g.tailLength = 4.0;
            break;
        case GlyphKind::Vesicle:
            g.leafletGap = 3.0;
            g.headRadius = 1.2;
            g.tailLength = 5.0;
            break;
        case GlyphKind::Capsule:
            g.cornerFraction = 0.5;
            break;
        case GlyphKind::Spiralling:
            g.turns = (spec.variant == 5) ? 4.0 : 3.0;
            g.amplitude = (spec.variant == 5) ? 0.30 : 0.55;
            g.leafletGap = 3.0;
            break;
        case GlyphKind::Pore:
            g.subunitRadius = 1.8;
            break;
        case GlyphKind::Barrel:
            g.barrelAngleDeg = 12.0;
            break;
        case GlyphKind::Antibody:
            g.chainLength = 12.0;
            g.disulfides = (spec.variant == 4) ? 0.0 : 2.0;
            break;
        case GlyphKind::Receptor:
        case GlyphKind::Channel:
            g.tmSegments = 0.0;   // 0 == 'use the variant table'
            g.ligandPresent = (spec.variant == 2 || spec.variant == 8) ? 1.0 : 0.0;
            break;
        case GlyphKind::DoubleHelix:
            g.basePairs = 12.0;
            g.turns = 2.0;
            g.amplitude = 1.0;
            g.helixRadius = 3.2;
            g.risePerTurn = 10.0;
            break;
        case GlyphKind::Strand:
            g.amplitude = 0.35;
            break;
        case GlyphKind::Star:
            g.nodes = 5.0;
            g.innerRatio = 0.45;
            break;
        case GlyphKind::Polygon:
            g.cornerFraction = (spec.variant == 3) ? 0.0 : 0.02;
            break;
        case GlyphKind::Cloud:
            g.nodes = 9.0;
            g.amplitude = 0.18;
            break;
        case GlyphKind::Virus:
            g.capsidFaces = (spec.variant == 0) ? 6.0 : 0.0;
            g.spikes = 10.0;
            break;
        case GlyphKind::Bacteriophage:
            g.spikes = 6.0;
            g.capsidFaces = 6.0;
            break;
        case GlyphKind::Plasmid:
            g.sweepDeg = 360.0;
            break;
        default:
            break;
    }
}

} // namespace biodraw
} // namespace r62

