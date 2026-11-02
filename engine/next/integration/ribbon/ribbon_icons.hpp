#ifndef R62_T19_RIBBON_ICONS_HPP
#define R62_T19_RIBBON_ICONS_HPP
// Vector icons drawn with plain GDI. No vendor assets are copied; every glyph is
// constructed from lines/arcs/polygons at the requested size.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace r62 { namespace integration { namespace ribbon {

enum IconId {
    IconNone = 0,
    IconCursor, IconLasso, IconEraser,
    IconBond, IconBondDouble, IconBondTriple, IconBondQuad, IconBondAny,
    IconBondWedge, IconBondHash, IconBondHollowWedge, IconBondBold,
    IconBondDashed, IconBondDotted, IconBondWavy, IconBondDative,
    IconRing3, IconRing4, IconRing5, IconRing6, IconRing7, IconRing8,
    IconRingAromatic, IconRingDiene, IconChair,
    IconChain, IconChainSnake,
    IconText, IconTextBold, IconTextItalic, IconTextUnderline, IconTextSub, IconTextSup,
    IconArrowLarge, IconArrowSmall, IconArrowHollow, IconArrowEquilibrium,
    IconArrowUnbalanced, IconArrowResonance, IconArrowNoGo, IconArrowRetro, IconArrowArc,
    IconRect, IconOval, IconCircle, IconRoundRect, IconLine, IconArc,
    IconCurve, IconEditCurve,
    IconBracketSquare, IconBracketParen, IconBracketCurly, IconDagger,
    IconPlus, IconMinus, IconRadical, IconDoubleRadical, IconRadicalCation,
    IconRadicalAnion, IconHDot, IconHDash, IconAttachment, IconPushElectron,
    IconOrbitalS, IconOrbitalP, IconOrbitalSigma, IconOrbitalSpLobe,
    IconOrbitalSp3, IconOrbitalD,
    IconQuery, IconStereoAbs, IconStereoRac, IconStereoRel, IconMap,
    IconFragment, IconPerspective, IconTable, IconTemplate, IconPlate,
    IconBioEnzyme, IconBioGProtein, IconBioReceptor, IconBioIonChannel,
    IconBioImmunoglobin, IconBioNuclearPore, IconBioDNA, IconBioRNA,
    IconBioMembrane, IconBioOrganelle, IconBioRibosome, IconBioVirus,
    IconBioBacterium, IconBioPlasmid, IconBioProtein, IconBioCloud, IconBioShape,
    IconUndo, IconRedo, IconRepeat, IconCopy, IconCut, IconPaste,
    IconDelete, IconSelectAll,
    IconZoomIn, IconZoomOut, IconZoomFit, IconZoomActual, IconPan,
    IconAlignLeft, IconAlignCenterH, IconAlignRight, IconAlignTop,
    IconAlignCenterV, IconAlignBottom,
    IconDistributeH, IconDistributeV, IconGroup, IconUngroup,
    IconFront, IconBack, IconForward, IconBackward,
    IconRotateL, IconRotateR, IconFlipH, IconFlipV, IconMerge,
    IconColor, IconLineWidth, IconFill, IconGrid, IconPanel, IconHost,
    IconHelp, IconFile, IconOpen, IconSave, IconExport, IconNew, IconLink,
    IconPropBond, IconPropAtom, IconPropText,
    IconCount
};

// Draw one icon centred in the box [x,y,x+size,y+size]. 'variant' carries a
// size/degree/style sub-value for icons that need one (ring size, arc degrees,
// arrow length class). Pure GDI; caller owns the DC.
void drawIcon(HDC dc, int icon, int variant, int x, int y, int size, COLORREF fg, COLORREF accent);

}}}
#endif
