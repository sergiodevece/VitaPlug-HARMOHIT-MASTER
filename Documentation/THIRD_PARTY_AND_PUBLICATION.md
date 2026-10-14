# Third-party and public-publication notes

## Repository publication state

This repository is intended to be visible publicly for study and presentation. No `LICENSE` file is included, by the project owner’s explicit decision. Public visibility alone does not grant a general license to reuse, modify or redistribute VitaPlug HARMOHIT code or visual assets.

## JUCE

HARMOHIT uses JUCE headers and CMake targets, but does not include JUCE source in this repository. `CMakeLists.txt` expects a local JUCE 9.0.3 checkout supplied through `JUCE_DIR`.

JUCE is licensed separately by its provider. Anyone building, distributing or otherwise using this project must review the then-current JUCE licensing terms and satisfy the option that applies to their use, including any applicable commercial or GPL obligations. This repository does not provide JUCE rights, a JUCE sublicense, legal advice or a claim that a particular distribution model is compliant.

## Assets and dependencies

The HARMOHIT panel artwork and visual-check images are included as project assets. They are not offered under a separate reuse license. Before adding a dependency, binary, sample, font, image or code contribution to the public repository, record its source and license and confirm it can be published.

## Publication hygiene performed for this snapshot

- Checked tracked source, documentation and configuration for common credential/private-key patterns.
- Confirmed that the product repository does not vendor JUCE or compiled plug-in bundles.
- Added ignore rules for local environment files and common credential-bearing local files.
- Kept the existing Git history and branches intact; no reset, clean, force push, rebase or deletion was used.

The commit history currently contains the literal placeholder author address `TU_EMAIL_DE_GITHUB`. It is not a credential, but it will be visible if the complete history is published. It is preserved because rewriting existing history is outside this publication preparation and would conflict with the project’s preservation requirement.

## HARMOHIT MASTER v0.2.0 (R2) — publication notes

### Project code and assets

The decision above applies unchanged to HARMOHIT MASTER. There is no `LICENSE` file: the repository is public for study and presentation, and public visibility does not grant reuse, modification or redistribution rights. The MASTER panel artwork (`Resources/HARMOHIT/HarmoHitMasterApproved.png`) and the screenshots are project assets under the same terms. No font files are distributed: panel lettering is part of the artwork.

### JUCE 9.0.3

The JUCE Framework modules are dual-licensed: **AGPLv3** or the commercial **JUCE 9 End User Licence Agreement** (<https://juce.com/legal/juce-9-licence/>). JUCE is not included in this repository. Compiled plug-ins contain JUCE code, so **distributing binaries** requires a valid basis: either AGPLv3, which would require releasing the corresponding source under AGPLv3 (incompatible with the no-licence decision above), or the JUCE 9 EULA in a tier that applies to the owner (Starter, Indie, Pro…, with the conditions published by JUCE). Choosing that basis is the owner's decision. This document does not provide legal advice.

### VST3 SDK (bundled with JUCE)

The VST3 SDK included in JUCE 9.0.3 is distributed by Steinberg under the MIT License. Its notice must accompany copies of the software that contain it, such as the VST3 binary:

```
MIT License

Copyright (c) 2025, Steinberg Media Technologies GmbH

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Trademarks and technical references

"Fairchild" and "Fairchild 670" are trademarks of their respective owners. They are cited only as a technical and historical reference for the variable-mu behaviour that inspired the DRIVE 670 engine. VitaPlug HARMOHIT MASTER is an independent product. It is not an emulation of that hardware and is not affiliated with, sponsored or endorsed by those trademark owners. Audio Units is a trademark of Apple Inc.; VST is a trademark of Steinberg Media Technologies GmbH.

### Material deliberately not published

- The user's mixes and Logic captures used as test evidence (`evidencias/`, ignored by Git).
- The DRIVE 670 LAB prototype project and its reference audio. Its reference loops come from Apple Loops, which are licensed for music production, not for redistribution as samples.
- Local backups, builds and installed binaries.

The DRIVE 670 equivalence suite (`HarmoHitDrive670IntegrationTests`) is only built when that private prototype project is present locally.

### Hygiene performed for v0.2.0

- Scanned the files to be published for credential, private-key and token patterns: none found.
- Scanned for e-mail addresses: none found in project files.
- Replaced local absolute paths in the new reports with repository-relative or `<VITAPLUG>/…` references. One inherited MIX report keeps a historical path that is already public in the MIX history.
- The history still contains the literal placeholder author address noted above. New commits use the same configured identity.
