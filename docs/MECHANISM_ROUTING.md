# Electron arrow routing (2026-11-09, v3)

The native arrow renderer uses a filled triangular head for electron-pair arrows. A single-electron arrow retains one filled side. Head direction follows the last route segment; heads, bounds, hit testing, preview and committed rendering consume the same geometry.

## Routing

1. Resolve endpoints to the nearest atom or bond centre (manual gestures), or explicit atom/bond references (AI).
2. Score compact circular arcs with a common preferred-curvature term and soft clearance cost against bond capsules, atom-label discs, estimated text extents, paths and existing arrows.
3. Retain a compact clear arc for local electron transfers. For blocked spans or a large detour, search a bounded grid using A*. A state includes its incoming direction. Cost is path length plus squared turning penalty; the Euclidean heuristic is admissible. Grid edges are cached and sampled for clearance, including diagonals.
4. Remove unnecessary grid points using collision-tested visibility. Replace corners by quadratic Bezier segments, shrinking rounding radii until clearance is preserved. Prefer an open clamped cubic B-spline over that route, trying successively denser support points until a collision-free continuous spline is found. Interior cubic spans are C2 continuous. Retain the verified rounded route when the corridor is too tight. Recheck the complete smoothed path before accepting it.
5. Trim the visible stroke around printed element labels. Store the route in chord-relative coordinates so translation, rotation and scaling remain coherent. Route data is serialized in the native arrow extension; undo and redo use the existing transaction model.

## Interaction

Choose a curved arrow, press at the source and drag to the destination. The preview and committed arrow use the same router. Alt bypasses snapping and automatic routing. Endpoint handles resize the native arrow. The middle handle currently replaces a routed curve with a manually shaped circular arc; arbitrary local Bezier-handle editing is not implemented.

## Bounds and fallback

Grid spacing is at least 2.5 document points, with at most approximately 101 cells on the longer axis and a bounded search count. The search can return no route; the best scored arc remains available for manual adjustment. This is a bounded geometric heuristic, not a guarantee of an optimal chemical diagram. Text bounds are approximate. Existing arrow curve obstacles use their full sampled route.

Mechanism arrows retain explicit atom/bond electron-source and electron-target references. Moving one endpoint rebuilds from chemical anchor positions, excluding the old arrow from routing obstacles; clipped endpoints and the old curve are not reused as routing input. Rigid moves retain the translated curve while its glyph clearance remains valid. Multi-bend route data round-trips in MoleculeStudio CDXML via its extension. Other CDXML consumers may show only the circular fallback; cross-application curved-path export is not certified.

## Gesture-shaped curves (2026-11-13)

The pointer trace selects the preferred side once it bows more than three device pixels away from the chord. The preference participates in compact-curve scoring and grid-search costs; actual obstacles can override it. Departure and arrival tangents are estimated from the trace and stored in chord coordinates, together with the side preference, in the native arrow params (and CDXML extension). Reversal swaps these tangents and reverses the side.

Long spans consider cubic Bezier curves with independently directed end tangents, including smooth inflections. Clear default long arrows use a variable-curvature S rather than a constant-radius arc. Short bond returns score turning radius for both electron-pair and fishhook arrows; circular candidates keep compact reversals round, including returns to unlabeled skeletal carbons.

## Tests

Native `--ai-check` with the mechanism fixture tests alternating obstacles, post-smoothing clearance, route serialization, reversal, manual drag/undo and CDXML round-trip. `tools/test_mechanism_plugin.py` checks the Python API. The previously observed full-suite CDX failure is separate from these passing targeted checks and has not been declared resolved.

## Sharp-tip rendering and naming

Full solid arrow shafts now terminate inside the head. Filled heads render without an outline stroke, so neither the shaft end cap nor a bevel join can protrude from the triangle tip. The same rule applies to previews. Graphics exposes a direct 机理箭头 button. The 机理箭头 menu contains only this automatic tool; reaction arrows and equilibrium/resonance are grouped by purpose, and legacy arcs are under 更多样式 → 弧线样式.

### Tall short-return correction

For electron-pair bond returns, consider convex half ellipses with independent height and width. Score arch fullness against the visible endpoint chord, and reject ellipses whose apex radius would be too tight. Circular and cubic candidates remain available for cramped or obstructed cases. Fishhook routing retains its own shape preference.

The filled-head renderer now trims the original curve into its tangent-aligned arrowhead. It no longer replaces the end of a mechanism shaft with a second fitted cubic neck, which could create an unintended inflection near the top of a short arch. The curvature regression checks the actual rendered shaft against the routed curve, and includes an explicit oblique H-O bond beside a wedge to carbon.

### Crowded H-O and long gesture correction

Compact neutral bond returns may widen the shoulders of a tall arch using a smooth horizontal correction to the ellipse. This preserves apex radius when a narrow endpoint chord previously rejected every tall candidate. Charged targets keep the existing radial docking constraint.

Long curves blend abrupt mouse-end headings with the preferred side and score turning radius relative to span. Grid search is invoked for actual obstruction, rather than solely because of soft proximity cost or a stale circular fallback sagitta. The interactive regression reconstructs the base-to-H attack plus a compact H-O return above two fused rings and saves the combined drawing.

### Shallow head recess and finite-curve alignment

Full mechanism heads now use a shallow tail recess (14% of head length). Their axis follows the chord from the tip to the original curve at 85% of head length, so the head aligns with the incoming visible stroke rather than the hidden terminal tangent. The shaft remains the routed curve and ends inside the head. Tests check notch depth, notch-to-curve distance, glyph entry and actual rendered clearance. The short-return candidate shifts its bend slightly toward the source to lengthen the descending leg.

### Early bend and eased arrival (2026-11-14)

For longer cubic mechanism arrows, scale the source control distance by 0.78 and the target control distance by 1.22. Both end headings and the established head silhouette are retained. This places more bending near the electron source and gives the head a longer, straighter approach. A reference hook regression measures turning per unit arc length in the first 45% and last 30% of the curve at three scales and two rotations, in addition to existing obstacle, movement and serialization checks.
