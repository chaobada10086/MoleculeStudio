# MoleculeStudio trademark and name policy

Version 1.0 - applies to MoleculeStudio 0.9.4.1 and later.

This policy explains what you may do with the MoleculeStudio name and logo. It is
separate from the copyright licence: the Apache License 2.0 in [LICENSE](LICENSE)
governs the code, but section 6 of that licence explicitly does **not** grant
permission to use the trade names, trademarks, service marks or product names of
the licensor.

## 1. The marks

| Mark | What it is | Status |
| --- | --- | --- |
| **MoleculeStudio** | The project and product name (one word, capital M and S) | Unregistered; claimed by the project as a common-law mark |
| The MoleculeStudio logo and application icon | The artwork in `Icons/` and the icon embedded in `MoleculeStudio.exe` | Copyright of the project; no trademark registration |

No registration is claimed in any jurisdiction, and nothing in this document
should be read as a promise that a registration exists or will be obtained. The
marks are used to identify the origin of the software, not to restrict
interoperability.

## 2. Uses that are always allowed (no permission needed)

* Referring to the project by name in articles, papers, reviews, tutorials,
  course material, comparison tables and conference talks.
* Stating truthfully that your file format, plugin or service is "compatible
  with MoleculeStudio" or "works with MoleculeStudio".
* Redistributing an **unmodified** copy of an official release, including the
  portable package, under its own name and with its own LICENSE, NOTICE and
  THIRD_PARTY_NOTICES.md intact.
* Using the name as a package name for an unmodified repackaging, for example
  `moleculestudio-portable-0.9.3.2.zip`, as long as the contents are unmodified.
* Academic citation of the project.

## 3. Uses that need a clear qualifier

If you distribute a **modified** build, you must make the difference obvious.
Pick one of these:

1. **Rename the product.** E.g. "MolEdit", "StructureBench". This is the cleanest
   option and is required if you change the chemistry behaviour, the file format
   handling or the bundled engines.
2. **Keep the name with an explicit qualifier**, for example
   "MoleculeStudio Community Edition (unofficial fork)" or
   "MoleculeStudio 0.9.4.1-mod (not affiliated with the MoleculeStudio
   project)". The qualifier must appear in the About box, the window title, the
   README and the download page - not only in the source tree.

In both cases the modified build must:

* keep [LICENSE](LICENSE), [NOTICE](NOTICE) and
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and state what was changed
  (Apache-2.0 section 4(b));
* not use the MoleculeStudio logo or icon as its own application icon;
* not claim or imply that the MoleculeStudio project endorses, maintains or
  supports it;
* not use a domain name, GitHub organisation, store listing or social handle
  that a reasonable user would mistake for the official project.

## 4. Logo rules

The logo and icon artwork is licensed under Apache-2.0 as part of the source
tree, but trademark rights are not granted. You may:

* use the unmodified logo to link to the official project or to identify an
  unmodified official release;
* reproduce it in documentation that describes the official software.

You may not:

* alter, recolour, crop or add effects to the logo;
* combine it with another logo to imply a partnership;
* use it as the icon of a fork, a plugin, or a commercial product;
* register it, or a confusingly similar mark, as a trademark, domain name or
  social media handle.

## 5. Not affiliated with Revvity, ChemDraw or DeepSeek

MoleculeStudio is an independent project. It is **not affiliated with, endorsed
by, sponsored by or approved by**:

* **Revvity, Inc.** or its **ChemDraw** product line (formerly PerkinElmer
  Informatics). ChemDraw and Revvity are trademarks of their respective owners.
  MoleculeStudio reads and writes CDXML. It ships no ChemDraw code, no ChemDraw
  binary and no ChemDraw-derived structure library. The name "ChemDraw" appears
  only to describe the file format and that interoperability.
* **DeepSeek.** The project bundles the DeepSeek Harness runtime and can call a
  DeepSeek API using a key supplied by the user. "DeepSeek" and the DeepSeek
  logo are trademarks of their owner. MoleculeStudio is not a DeepSeek product
  and is not supported by DeepSeek.
* **Microsoft Corporation.** Word/PowerPoint OLE editing requires the user's own
  Microsoft Office installation. No Microsoft binaries are redistributed except
  the Visual C++ runtime DLLs, which are redistributed under the Microsoft
  Visual Studio licence terms.
* **Oracle Corporation, Eclipse Foundation / Adoptium, The Document Foundation,
  OpenJS Foundation.** The bundled Java runtime is an unmodified Eclipse Temurin
  build; the bundled office engine is a prebuilt LibreOffice Kit; Node.js is an
  unmodified upstream binary. None of these organisations sponsors this project.
* **The RDKit project, the OPSIN project, the NISPO project, the CSP5 authors.**
  These are upstream open-source components, used unmodified through their
  public interfaces.

## 6. Third-party marks used in this project

The following marks belong to their owners and are used only descriptively:

ChemDraw(R), Revvity(R), PerkinElmer(R), Microsoft(R), Windows(R), Office(R),
DeepSeek(R), Oracle(R), Java(R), Eclipse Temurin(TM), Adoptium(TM),
LibreOffice(R), The Document Foundation(TM), Node.js(R), OpenJS Foundation(R),
RDKit, OPSIN, NISPO, CSP5, PyTorch(R), NumPy, SciPy, pandas, scikit-learn,
OpenCV, FFmpeg, libvips, PDFium, SQLite, OpenSSL, Tcl/Tk, zlib, Python.

No ownership of these marks is claimed.

## 7. Reporting misuse

If you believe the MoleculeStudio name or logo is being used in a confusing or
unauthorised way, or if you want permission for a use that this policy does not
allow, open an issue in the project tracker or write to the maintainers at
<contact address to be filled in before publication>.

## 8. Changes to this policy

The project may revise this policy. The version published in the repository at
the time you distribute a build is the version that applies to that build.
