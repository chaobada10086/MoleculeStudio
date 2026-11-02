# OCSR third-party notices

This add-on bundles MolScribe and a frozen CPU Python runtime. The following
components are new relative to MoleculeStudio 0.9.4.1. License texts are in
`ocsr_runtime\LICENSES\` and in each package's `.dist-info` directory.

| Component | Version | License | Source |
|---|---|---|---|
| MolScribe | 1.1.1 | MIT | https://github.com/thomas0809/MolScribe |
| MolScribe weights | swin_base_char_aux_1m680k / 1m | MIT (model card) | https://huggingface.co/yujieq/MolScribe |
| PyTorch | 2.5.1+cpu | BSD-3-Clause | https://pytorch.org |
| torchvision | 0.20.1+cpu | BSD-3-Clause | https://github.com/pytorch/vision |
| timm | 0.4.12 | Apache-2.0 | https://github.com/huggingface/pytorch-image-models |
| Albumentations | 1.1.0 | MIT | https://github.com/albumentations-team/albumentations |
| OpenNMT-py | 2.2.0 | MIT | https://github.com/OpenNMT/OpenNMT-py |
| SmilesPE | 0.0.3 | MIT (PyPI metadata) | https://pypi.org/project/SmilesPE |
| RDKit | 2024.9.6 | BSD-3-Clause | https://github.com/rdkit/rdkit |
| NumPy | 1.26.4 | BSD-3-Clause | https://numpy.org |
| pandas | 2.2.3 | BSD-3-Clause | https://pandas.pydata.org |
| matplotlib | 3.9.2 | PSF-based | https://matplotlib.org |
| SciPy | 1.13.1 | BSD-3-Clause | https://scipy.org |
| scikit-image | 0.24.0 | BSD-3-Clause | https://scikit-image.org |
| Pillow | 12.3.0 | HPND | https://python-pillow.org |
| pypdfium2 | 5.14.0 | Apache-2.0 / BSD-3-Clause (PDFium) | https://github.com/pypdfium2-team/pypdfium2 |
| huggingface_hub | 2.4.0 | Apache-2.0 | https://github.com/huggingface/huggingface_hub |
| OpenCV (headless) | 4.5.5.64 | Apache-2.0 | https://opencv.org |

## Model weight provenance

- `swin_base_char_aux_1m680k.pth`: 1,134,173,494 bytes, SHA-256 `32821fa4ba8d17f9a92e374601438c5e757947dbf1141e9ad0ef51cccfa1c501`, downloaded from the MIT-licensed `yujieq/MolScribe` Hugging Face repository.
- `swin_base_char_aux_1m.pth`: 1,134,940,406 bytes, SHA-256 `6f0df56fa32b5ffc21f8c7f311ef333da522f590bf5622e966c6bcb1f2d9ea1d`.

## Evaluated, deliberately not bundled

- **COMO** (https://huggingface.co/Keylab/COMO): model card declares **CC-BY-NC-4.0**. It is not included in this distribution and must not be used as a commercial component without separate permission.
- **MolSight** (https://github.com/hustvl/MolSight): not selected for the first version; not downloaded or bundled.

## Data handling

Recognition is fully local. The service only reads the PDF/image path chosen in
the native file dialog and writes caches/records under the program directory.
It does not send the document, crops, or results to any third-party service.
