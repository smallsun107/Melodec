# Bundled fonts

Three Open Font License faces are linked into the GUI binary, so Chinese /
Korean / Russian / accented-Latin text in paths and song titles renders without
depending on any system font:

| File | Role | License |
| --- | --- | --- |
| `NotoSansSC-Regular.ttf` | **[0] primary** — CJK (incl. Extension A) + Latin | `OFL-NotoSansSC.txt` |
| `NotoSans-Regular.ttf` | [1] fallback — Latin-Ext, Greek, Cyrillic, combining marks, Vietnamese | `OFL-NotoSans.txt` |
| `NotoSansKR-Regular.ttf` | [2] fallback — Hangul + Hanja | `OFL-NotoSansKR.txt` |

All three are merged into a single `ImFont` with `ImFontConfig::MergeMode`;
ImGui looks up each codepoint in the primary face first, then the fallbacks.
(Terminals get this for free through OS font fallback — ImGui does not, which is
why multi-script support has to be assembled explicitly.)

## No subsetting

The fonts are embedded **in full**. Earlier we subset them to hand-picked
Unicode ranges, which repeatedly dropped scripts and produced `?` in real file
names — CJK first, then Hangul, then Cyrillic and combining accents:

```
Rauf & Faik - это ли счастье.ncm      (Cyrillic)
July - 사랑에 베이다.ncm                  (Hangul)
Hoaprox - #Lov3 #Ngẫu Hứng.ncm         (NFD: e + U+0301, U+0303, U+031B …)
```

Measured, subsetting the CJK face saved **0.1 MB** of gzip — not worth the risk.
Full faces cost ~9.2 MB gzipped in total (binary ≈ 10 MB) and make the whole
class of "missing range" bugs impossible.

The **only** transformation applied is instancing the upstream *variable* fonts
to a static Regular (`assets/fonts/instance.sh`). This is required: without it
stb_truetype reads the font's default master, which is **Thin** for Noto Sans
SC/KR, and the UI would render thin text.

## Coverage

| Font | Codepoints | Notes |
| --- | ---: | --- |
| Noto Sans SC | 30,890 | CJK Unified + Extension A + Latin; also carries partial Greek (49) / Cyrillic (66) / Latin-Ext, but only 5 combining marks |
| Noto Sans | 3,094 | Latin Extended A/B, IPA, spacing modifiers, Greek, **Cyrillic + Supplement**, **Combining Diacritical Marks (112)**, Latin Extended Additional (Vietnamese) |
| Noto Sans KR | 23,174 | Hangul syllables/jamo + Hanja + Latin |

Verified against a real 359-file library (7.6 GB): **zero** unsupported
codepoints across all file names and all metadata (title / artist / album).

## How they were produced

```sh
./instance.sh          # downloads the upstream variable fonts, instances wght=400
```

To add another script (e.g. Arabic, Thai, Devanagari), drop a Noto face here and
append it to `RENCM_FONTS` in `CMakeLists.txt` — nothing else needs changing.

## Reserved Font Name note

Noto Sans SC's OFL carries a **Reserved Font Name: "Source"** (Source Han Sans
lineage). OFL forbids using an RFN in a *modified* font's name, so derivatives
must not be called with "Source" in them. Our file names and the internal family
names (e.g. `Noto Sans SC`) do not contain it, so this is compliant.
