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

### JUCE 9.0.3 — licence basis for the binaries

The JUCE Framework modules are dual-licensed: **AGPLv3** or the commercial **JUCE 9 End User Licence Agreement** (<https://juce.com/legal/juce-9-licence/>).

**Decision of the owner (2026-10-14).** VitaPlug HARMOHIT MASTER binaries are distributed under the commercial **JUCE 9 Starter** licence, the free tier, by an individual developer who is the Owner of the Product. The owner confirmed that they currently obtain **no revenue or funding** from software developed with JUCE; the Starter limit is 20,000 USD over the previous 12 months (EULA §1.2.1). The project is **not** relicensed under AGPLv3, and the AGPLv3 option of JUCE is not used.

| Item | Licence |
|---|---|
| HARMOHIT MASTER own source code and artwork | No licence: all rights reserved, public for study and presentation |
| JUCE modules compiled into the binaries | JUCE 9 EULA, Starter tier (commercial, free) |
| Third-party components inside JUCE that are compiled into the binaries | Their own licences (MIT, BSD-3-Clause, Apache-2.0, Zlib, libpng-2.0), with notices in `Documentation/THIRD_PARTY_NOTICES.txt` and in the release package |

Starter conditions reviewed against the EULA text:
- Revenue or funding ≤ 20,000 USD over 12 months (§1.2.1). If it is exceeded, an Indie or Pro licence is required (§1.14).
- Starter may not be mixed with other licence types (§1.13).
- The Framework is not distributed on its own (§1.17): this repository does not include JUCE.
- The product does not create other products (§1.12).
- Nothing may cause the Framework to become subject to an open-source licence requiring disclosure (§2.3): the project grants no open-source licence.
- JUCE notices are not removed or altered (§2.9).
- Usage records must be kept and provided if RMS requests them (§10.3).
- The EULA requires no splash screen or attribution. RMS may cite the licensee's name, with an opt-out (§10.2).

This is a documented owner decision, not legal advice. Anyone building this source code needs their own JUCE licence.

### Third-party components compiled into the binaries

Verified by inspecting the symbols of the v0.2.0 binaries:

| Component | Licence | Binaries |
|---|---|---|
| VST3 SDK 3.8.0 | MIT | VST3 |
| AudioUnitSDK 1.1.0 | Apache-2.0 | AU |
| zlib 1.3.2 | Zlib | AU, VST3 |
| libpng 1.6.58 | libpng-2.0 | AU, VST3 |
| libwebp 1.6.0 | BSD-3-Clause | AU, VST3 |
| FLAC 1.5.0, libogg 1.3.6, libvorbis 1.3.7, Opus 1.6.1 | BSD-3-Clause | AU, VST3 |
| HarfBuzz 14.2.1 | MIT (Modern Variant) | AU, VST3 |
| SheenBidi 2.9.0 | Apache-2.0 | AU, VST3 |
| LunaSVG 3.5.0, PlutoVG 1.3.2 | MIT | AU, VST3 |

The full notices are in `Documentation/THIRD_PARTY_NOTICES.txt`, which is also shipped in the release ZIP.

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
- The history up to v0.2.0 (commit ab739e8) contains the literal placeholder author address noted above. It is not rewritten, to avoid force-pushing published history. The owner has since configured a real Git identity, which later commits use.
