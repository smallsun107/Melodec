# NCM Container & Cipher — Reverse-Engineering Analysis

> Goal: recover how the NetEaseMusic macOS client decrypts `.ncm` files, and
> document an independently reproducible implementation.
>
> Result up front: the payload inside a `.ncm` is an **MP3**. The box key is
> derived with **`key section XOR 0x64` → `AES-128-ECB` → `PKCS#7 unpad` → strip
> prefix**, and the audio is then streamed through **NCM's own two-index state-box
> XOR** (not standard RC4 PRGA).

The analysis below was performed on locally owned sample files, for format and
algorithm research only. Addresses and decompilation come from one specific build
of the client, so treat them as *evidence for this write-up*, not as stable ABI.

---

## 0. Contents

1. Target and environment
2. Sample facts
3. Tooling and IDA workflow
4. Locating the real decryption code (including the wrong turns)
5. NCM container format (verified byte by byte)
6. Key derivation (box key)
7. Audio decryption (NCM's state-box XOR)
8. Metadata (ID3 comment) decryption
9. Complete pseudocode
10. Reproducing it
11. Verification
12. Address / field quick reference
13. Traps and corrections

---

## 1. Target and environment

| Item | Value |
| --- | --- |
| Target | NetEaseMusic for macOS (main executable) |
| Binary | `<NeteaseMusic.app>/Contents/MacOS/NeteaseMusic` |
| Architecture | Mach-O x86_64, imagebase `0x100000000`, `__TEXT` size `0xfab7b0` |
| Sample | `sample.ncm` |
| Authorisation | local files, used only to study the format and algorithms |

The hash of the analysed build is deliberately omitted: the addresses below are
tied to one build and are not meant to identify or help obtain it.

---

## 2. Sample facts

```
magic        CTENFDAM
version      1
key_len      144
meta_len     726
crc          0x9bddd820
cover        38969 bytes JPEG (747x747)
audio start  0x9bbe
audio length 7448076 bytes
audio plain  MP3 / 320 kbps / 44.1 kHz / Stereo / 186.14 s
```

---

## 3. Tooling and IDA workflow

Two IDA channels were used, either works:

1. `ida-pro-mcp` — started from the IDA GUI via `Edit -> Plugins -> MCP` (macOS
   shortcut `Ctrl+Option+M`); tools such as `decompile_function`,
   `search_strings`, `get_xrefs_to`.
2. `ida_*` (idalib / ida-domain, direct) — `ida_execute_python`, with
   `db = Database.open()`, `db.pseudocode.decompile(ea)`, `db.xrefs.to_ea(ea)`,
   `ida_bytes.find_bytes(...)`.

Every address and decompilation in this document came from one of those two.

---

## 4. Locating the real decryption code

### 4.1 Start: string and constant search

Search the binary for NCM-related constants (script in Appendix A):

| Searched for | Hit |
| --- | --- |
| `CTENFDAM` | `0x1007cdb8f` (only occurrence) |
| `neteasecloudmusic` | `0x100b8aa2d` |
| `163 key(Don't modify):` | `0x100b918eb` |
| `hzHRAmso5kInbaxW` (16-byte key constant) | `0x100b417f8` |

`CTENFDAM` occurs exactly once in the whole binary, so **the module containing
that string is the NCM container parser**.

### 4.2 Class name / RTTI search

Functions whose symbols contain the relevant keywords:

- `+[NMMediaSupportedExtension isNCMFileExtension:]` `0x10009daad`
- `+[NMMediaSupportedExtension getNCMFileKey]` `0x10009db24`
  (literally `return CFSTR("neteasecloudmusic")`)
- `-[YYYID3Handler commentStringFromCryptedString:]` `0x10017b6b1`
  (handles `163 key(Don't modify):` inside the ID3 comment)
- `+[NMID3Cypher decryptString:]` `0x100103d2e` (metadata decryption)
- `-[CloundMusicAudioFile initWithFilePath:client:]` `0x1007d73cd`
  (**the actual NCM file class**; the typo "Clound" is in the original symbol)

`CloundMusicAudioFile` is the entry point. Decompiling its `init` shows:

```
file = new NCMFile()
key  = append 'n','e','t','e','a','s','e','c','l','o','u','d','m','u','s','i','c'
file->vtable[0](fileIO, mode, ?, client)     // Open    -> sub_1007CBE32
file->vtable[2](key)                          // setKey  -> sub_1007CCA22
file->vtable[5](...)                          // get comment
```

NCMFile's vtable lives at `off_100CF1748`:

| Index | Function | Role |
| --- | --- | --- |
| 0 | `sub_1007CBE32` | Open |
| 2 | `sub_1007CCA22` | setKey / build the box |
| 3 | `sub_1007CCE86` | write |
| 8/9/10 | `sub_1007CCF96` / `CCFA0` / `CCFAA` | version / flags / stream decrypt |
| 11 | `sub_1007CD03A` | **read and decrypt the audio** |

### 4.3 Main call chain

```
-[CloundMusicAudioFile initWithFilePath:client:]      0x1007d73cd
  └─ Open                       sub_1007CBE32
       └─ header / section parse sub_1007CBEA2
            ├─ key section      sub_1007CD438
            ├─ meta section     sub_1007CD66E
            └─ cover section    sub_1007CD892
  └─ setKey("neteasecloudmusic")  sub_1007CCA22
       └─ derive box key           sub_1007CCDEC -> sub_1007CB47C -> sub_1007CB76E
            └─ RC4-KSA box         sub_1007CE900
  └─ readDataWithLength:         0x1007d7d59
       └─ vtable[11]              sub_1007CD03A
            └─ state-box XOR      sub_1007CEA18 -> sub_1007CEA22
```

### 4.4 A wrong turn worth recording

`CodedInputDataCrypt.cpp`, the `seek` assertion, the `AES_decrypt` assertion and
`sub_100321CCC` / `sub_100321488` are easy to mistake for the NCM audio cipher:

- `sub_100321CCC` contains `AES_decrypt(state, state, key)` plus a 16-byte state
  write-back;
- the assertion string is
  `m_position % AES_KEY_LEN == m_decrypter.m_number` (file `CodedInputDataCrypt.cpp`).

But the same assertion batch also references `MMKV_IO.cpp`, and the class has
**no vtable references at all** — it is **Tencent MMKV's AES-CFB data cipher**,
unrelated to NCM. Treating the audio as standard RC4 PRGA or as AES-CFB is the
wrong direction, and that is exactly where early attempts went.

---

## 5. NCM container format (verified byte by byte)

### 5.1 Layout

```
offset    length    meaning
0x00      8         "CTENFDAM"
0x08      1         version (1 in this sample)
0x09      1         reserved (0x6d in this sample)
0x0A      4 (LE)    key_len      (144)
0x0E      key_len   key_blob
          4 (LE)    meta_len     (726)
          meta_len  meta_blob
          4 (LE)    crc          (0x9bddd820, for verification)
          1         cover flag   (1)
          4 (LE)    cover size   (38969)
          4 (LE)    cover size, duplicate (38969)
          size      cover (opaque image data, see note)
          ...       audio        (0x9bbe in this sample)
```

> The cover is **not necessarily a JPEG**. This sample carries a JPEG (`ff d8 ff`),
> but NetEase also ships PNG covers - a decodable PNG header (`89 50 4e 47`) is
> easy to find in the wild. Treat the cover bytes as opaque image data and sniff
> them, rather than assuming a format.

### 5.2 Evidence in code

- The header check (`sub_1007CD438`) reads 14 bytes and compares:
  - `v33[0] == 0x4E455443` → bytes `43 54 45 4E` = `CTEN`
  - `v33[1] == 0x4D414446` → bytes `46 44 41 4D` = `FDAM`
  - `key_len` sits at `[10..13]` of that 14-byte buffer.
- The cover section (`sub_1007CD892`) reads a 9-byte header first
  (`flag(1) + size(u32) + size(u32)`), then `size` bytes of image. That matches
  the sample bytes `01 39 98 00 00 39 98 00 00 | ff d8 ff ...` exactly.

Cross-check with `xxd` on the raw sample:

```
00000370: 05 15 31 24 17 53 11 57 20 d8 dd 9b 01 39 98 00
00000380: 00 39 98 00 00 ff d8 ff
                 ^crc 0x378  ^flag 0x37c  ^size 0x37d/0x381  ^JPEG 0x385
00009bb0: a5 ee f2 6f 45 29 d0 9b 57 12 96 3f ff d9 1c 3f
                                              ^JPEG ends 0x9bbd -> audio 0x9bbe
```

The JPEG terminator `ff d9` lands exactly at `0x9bbc-0x9bbd`, and
`0x385 + 38969 = 0x9bbe` — consistent.

---

## 6. Key derivation (box key)

### 6.1 Steps

Input: `key_blob = raw[0x0E : 0x0E+key_len]` (144 bytes here).

```
1) XOR every byte with 0x64            # initial state = 0x64646464
2) AES-128-ECB decrypt                 # key A0 = "hzHRAmso5kInbaxW"
3) PKCS#7 unpad                        # last byte n -> drop n trailing bytes
4) strip the 17-byte prefix "neteasecloudmusic"
=> box_key
```

For this sample:

```
key_blob XOR 0x64  +  AES-128-ECB(A0)  =>
"neteasecloudmusic93706570517657204843876..."   (144 bytes)
last byte = 0x0F  -> drop 15 bytes -> 129 bytes
strip 17-byte prefix              -> box_key = 112 bytes
```

### 6.2 Evidence in code

**Where A0 comes from**: the data segment at `0x100b417f0` holds a 62-character
alphabet string:

```
"FKy9uSYMhzHRAmso5kInbaxWGDVLXQ8OgPCl6ZE4vN2jJed1..."
```

`push_backEc_0` (`0x1007cb70a`) takes `alphabet[i+8]` for `i = 0..15`, i.e.
`alphabet[8:24] = "hzHRAmso5kInbaxW"` — that is the AES key.

**AES call**, inside `sub_1007CB76E`:

- `sub_100705BB0(key, 128, schedule)` — AES-128 key expansion
- `sub_1007065F0(&in[i], &out[i], schedule)` — AES-ECB decrypt, one 16-byte block
- then `n17 = last byte`; if `2 <= n17 <= 16` → `resize(len - n17)` — **PKCS#7 unpad**

**Prefix strip**, inside `sub_1007CB47C`:

```
std::string::basic_string(&tmp, &__stra, __pos /* = prefix_len = 17 */, npos, &a);
*__str = tmp;    // i.e. __str = __stra.substr(17)
```

The 17 comes from the length of the string passed to
`setKey("neteasecloudmusic")`.

---

## 7. Audio decryption (NCM's own state-box XOR)

### 7.1 KSA (`sub_1007CE900`)

```
box[i] = i                (i = 0..255)
c = 0; kp = 0
for i in 0..255:
    tmp = box[i]
    c = (key[kp] + tmp + c) & 0xFF
    kp = (kp + 1) % len(key)
    box[i] = box[c]
    box[c] = tmp
```

### 7.2 Stream cipher (`sub_1007CEA22`)

```
for i in 0..len-1:
    j = (position + i + 1) & 0xFF
    a = box[j]
    out[i] = in[i] ^ box[(a + box[(a + j) & 0xFF]) & 0xFF]
```

- `position` is the global byte index (0 for a whole-file decrypt).
- This is NCM's **two-index** state box, **not** standard RC4 PRGA (standard PRGA
  is `j=(j+box[i])&0xff; swap; k=box[(box[i]+box[j])&0xff]`).
- Against the decompilation (`sub_1007CEA22`):

```c
v7 = box[(a4 + v6 + 1)];
a3[v6] = box[(v7 + box[(v7 + a4 + v6 + 1) & 0xff]) & 0xff] ^ a2[v6];
```

where `a4` = position and `v6` = the local index.

### 7.3 Read entry (`sub_1007CD03A`)

```c
v7 = *(a1 + 24);                              // box (256 bytes)
sub_1007CEA18(v7, buf, buf, *(a1+256), len);  // state-box XOR, in place
stream->read(buf, len);
```

`a1+24` is the 256-byte box allocated and filled during `setKey`.

---

## 8. Metadata (ID3 comment) decryption

### 8.1 Steps

```
1) XOR every byte of meta_blob with 0x63   # initial state = 0x63636363
2) strip the prefix "163 key(Don't modify):"
3) base64-decode the rest
4) AES-128-ECB decrypt, key "#14ljk_!\]&0U<'("
=> "music:{...json...}"
```

### 8.2 Evidence in code

- `sub_1007CD66E`: `n99 = 99; sub_1007CF252(state,&n99); sub_1007CF2D6(state,data,len)`
  — i.e. XOR 0x63.
- `+[YYYID3Handler commentStringFromCryptedString:]` `0x10017b6b1`: takes whatever
  follows `163 key(Don't modify):` and hands it to
  `+[NMID3Cypher decryptString:]`.
- `+[NMID3Cypher decryptString:]` `0x100103d2e`: base64-decodes first, then
  decrypts with a key assembled from a series of selectors, in order:
  `pound,_1,_4,l,j,k,underscore,exclamation,back_slash,bracket_right,ampersand,_0,U,less_than,apostrophe,paren_left`
  → key `#14ljk_!\]&0U<'(` (16 bytes).

The decrypted metadata has this shape (values genericised):

```json
{
  "musicId": "<id>",
  "musicName": "<title>",
  "artist": [["<artist>", "<id>"]],
  "album": "<album>",
  "bitrate": 320000,
  "duration": 186135,
  "format": "mp3"
}
```

---

## 9. Complete pseudocode

```text
function decrypt_ncm(path):
    raw = read(path)
    assert raw[0:8] == "CTENFDAM"

    key_len = u32le(raw, 0x0A)
    key_blob = raw[0x0E : 0x0E+key_len]
    p = 0x0E + key_len
    meta_len = u32le(raw, p)
    meta_blob = raw[p+4 : p+4+meta_len]
    p += 4 + meta_len
    crc = u32le(raw, p); p += 4
    cover_flag = raw[p]
    cover_len = u32le(raw, p+1)
    cover = raw[p+9 : p+9+cover_len]
    audio = raw[p+9+cover_len : ]

    # ---- box key ----
    x = bytes(b ^ 0x64 for b in key_blob)
    plain = AES128_ECB_decrypt(x, key="hzHRAmso5kInbaxW")
    plain = pkcs7_unpad(plain)                 # last byte n -> drop n bytes
    assert plain.startswith("neteasecloudmusic")
    box_key = plain[17:]

    # ---- 256-byte box ----
    box = list(range(256)); c = 0; kp = 0
    for i in range(256):
        tmp = box[i]
        c = (box_key[kp] + tmp + c) & 0xFF
        kp = (kp + 1) % len(box_key)
        box[i], box[c] = box[c], tmp

    # ---- audio ----
    out = bytearray(len(audio))
    for i in range(len(audio)):
        j = (i + 1) & 0xFF
        a = box[j]
        out[i] = audio[i] ^ box[(a + box[(a + j) & 0xFF]) & 0xFF]

    # ---- metadata ----
    m = bytes(b ^ 0x63 for b in meta_blob)
    seg = m[m.index("163 key(Don't modify):")+len("163 key(Don't modify):"):]
    blob = AES128_ECB_decrypt(base64_decode(seg), key="#14ljk_!\\]&0U<'(")
    return out, cover, blob                # out = MP3
```

---

## 10. Reproducing it

### 10.1 Dependencies

- `python3`
- `openssl` (CLI, for AES-128-ECB)
- optional `ffmpeg` / `ffprobe` for verification

### 10.2 Script

A one-shot script is provided at the repository root as `ncm_decrypt.py`:

```bash
python3 ncm_decrypt.py path/to/song.ncm
```

Output:

```
<name>.mp3     decrypted audio
<name>.jpg     cover
stdout         container fields, box_key length, format, sha256, metadata
```

### 10.3 Minimal standalone implementation

```python
import struct, subprocess, tempfile, base64

A0       = b"hzHRAmso5kInbaxW"
META_KEY = b"#14ljk_!\\]&0U<'("

def aes_ecb_dec(data, key):
    data = data[:len(data)//16*16]
    with tempfile.NamedTemporaryFile() as s, tempfile.NamedTemporaryFile() as d:
        s.write(data); s.flush()
        subprocess.run(["openssl","enc","-aes-128-ecb","-d","-K",key.hex(),
                        "-nosalt","-nopad","-in",s.name,"-out",d.name], check=True)
        d.seek(0); return d.read()

def unpad(d):
    n = d[-1]
    return d[:-n] if 2 <= n <= 16 and d[-n:] == bytes([n])*n else d

def box_of(key):
    b = list(range(256)); c = 0; kp = 0
    for i in range(256):
        t = b[i]; c = (key[kp]+t+c) & 255; kp = (kp+1) % len(key)
        b[i] = b[c]; b[c] = t
    return b

def ncm_dec(data, box):
    o = bytearray(len(data))
    for i in range(len(data)):
        j = (i+1) & 255; a = box[j]
        o[i] = data[i] ^ box[(a + box[(a+j) & 255]) & 255]
    return bytes(o)

raw = open("x.ncm","rb").read()
key_len = struct.unpack_from("<I", raw, 10)[0]
key_blob = raw[14:14+key_len]
p = 14 + key_len
meta_len = struct.unpack_from("<I", raw, p)[0]
meta_blob = raw[p+4:p+4+meta_len]
p += 4 + meta_len + 4                      # +crc
cover_len = struct.unpack_from("<I", raw, p+1)[0]
audio = raw[p+9+cover_len:]

plain = unpad(aes_ecb_dec(bytes(b ^ 0x64 for b in key_blob), A0))
box_key = plain[17:]
mp3 = ncm_dec(audio, box_of(box_key))
open("out.mp3","wb").write(mp3)

m = bytes(b ^ 0x63 for b in meta_blob)
mk = b"163 key(Don't modify):"
seg = m[m.index(mk)+len(mk):].split(b"\x00")[0].strip()
seg += b"=" * (-len(seg) % 4)
print(aes_ecb_dec(base64.b64decode(seg), META_KEY).decode("utf-8","replace"))
```

---

## 11. Verification

```
$ file "out.mp3"
Audio file with ID3 version 2.4.0, contains: MPEG ADTS, layer III, v1,
320 kbps, 44.1 kHz, Stereo

$ ffprobe ...
codec_name=mp3
sample_rate=44100
channels=2
format_name=mp3
duration=186.135918
bit_rate=320113

$ ffmpeg -v error -i out.mp3 -f null -
(no output, exit code 0)     -> full decode passes
```

SHA-256 of the decrypted audio (kept as the test vector):

```
ea30a900af1843433ff24459c0351b7076508d489296245ac1c2c9865c9d0026
```

---

## 12. Address / field quick reference

### 12.1 Functions

| Address | Name / role |
| --- | --- |
| `0x10009daad` | `+[NMMediaSupportedExtension isNCMFileExtension:]` |
| `0x10009db24` | `+[NMMediaSupportedExtension getNCMFileKey]` → `CFSTR("neteasecloudmusic")` |
| `0x100103d2e` | `+[NMID3Cypher decryptString:]` (metadata base64 + AES) |
| `0x10017b6b1` | `+[YYYID3Handler commentStringFromCryptedString:]` |
| `0x1007d73cd` | `-[CloundMusicAudioFile initWithFilePath:client:]` (NCM entry) |
| `0x1007d7d59` | `-[CloundMusicAudioFile readDataWithLength:]` |
| `0x1007d7e2b` | `-[CloundMusicAudioFile fetchImage]` |
| `0x1007cbe32` | NCMFile vtable[0] = Open |
| `0x1007cbea2` | header / section parse |
| `0x1007cca22` | vtable[2] = setKey, builds the box |
| `0x1007ccdec` | key assembly (calls `sub_1007CB47C`) |
| `0x1007cb47c` | combine + AES + `substr(17)` |
| `0x1007cb76e` | **AES-128-ECB decrypt + PKCS#7 unpad** |
| `0x1007cd03a` | vtable[11] = read and decrypt audio |
| `0x1007cea18` / `0x1007cea22` | **audio state-box XOR** |
| `0x1007ce900` | **256-byte box KSA** |
| `0x1007cd438` | read key section (XOR 0x64) |
| `0x1007cd66e` | read meta section (XOR 0x63) |
| `0x1007cd892` | read cover section (9-byte header + data) |
| `0x1007cf252` | state init (seed copied 4 times) |
| `0x1007cf276` | 4-byte state cyclic XOR |
| `0x100705bb0` | AES-128 key expansion |
| `0x1007065f0` | AES-128 single-block decrypt |

### 12.2 Data

| Address | Contents |
| --- | --- |
| `0x1007cdb8f` | string `CTENFDAM` |
| `0x100b417f0` | 62-char alphabet `"FKy9uSYMhzHRAmso5kInbaxWGDVLXQ8OgPCl6ZE4vN2jJed1..."` |
| `0x100b417f8` | `alphabet[8]` onwards — AES key `"hzHRAmso5kInbaxW"` |
| `0x100b8aa2d` | string `neteasecloudmusic` |
| `0x100b918eb` | string `163 key(Don't modify):` |
| `off_100CF1748` | NCMFile vtable |
| `0x100A44090` / `0x100B41A60` | box-init SIMD constants (`0..15`, step `0x10`) |

### 12.3 Values for this sample

| Field | Value |
| --- | --- |
| key_len | 144 |
| key section plaintext | `neteasecloudmusic93706570517657204843876...` |
| PKCS#7 padding | last byte `0x0F` (drop 15) |
| box_key length | 112 |
| meta_len | 726 |
| crc | `0x9bddd820` |
| cover start / length | `0x385` / `38969` |
| audio start / length | `0x9bbe` / `7448076` |

---

## 13. Traps and corrections

1. **Do not mistake MMKV for NCM**: `CodedInputDataCrypt.cpp` +
   `sub_100321CCC` (AES + 16-byte state) is MMKV's AES-CFB, and the mass of
   assertion strings makes it look relevant. It is not the audio stream cipher.
2. **The audio is not standard RC4**: the correct expression is the two-index
   `box[(box[j] + box[(box[j]+j)&0xff]) & 0xff]`. Standard PRGA
   (`j=(j+box[i])&0xff; swap; box[(box[i]+box[j])&0xff]`) produces output that
   "looks like LOAS" and then fails to decode — a convincing dead end.
3. **Unpad before stripping the prefix**: PKCS#7 first, then the 17-byte prefix.
   Doing only one of the two (or in the wrong order) yields random data. The
   padding length is the last byte of the AES plaintext.
4. **Container offsets are easy to get wrong**: `key_len` is at `0x0A` (not
   `0x08`); the cover section has a 9-byte header (`flag + size + size`); the
   cover length field sits at `+1` after the crc; audio starts at
   `cover start + cover length`.
5. **Do not accept "magic matches" or "starts with ID3" as success**: run a full
   `ffmpeg -f null -` decode and require exit code 0.
6. **AES must be `-nopad`**: `openssl enc -aes-128-ecb` unpads PKCS#7 by default;
   use `-nopad` and handle the padding yourself, or the length and content get
   trimmed twice.

---

## Appendix A: IDAPython snippets used to locate things

```python
import ida_bytes, ida_idaapi
from ida_domain import Database
db = Database.open()
BAD = ida_idaapi.BADADDR
base, end = 0x100000000, 0x100000000 + 0xfab7b0

def find_all(pat):
    res, ea = [], base
    while True:
        f = ida_bytes.find_bytes(pat, ea, end - ea)
        if f is None or f == BAD: break
        res.append(f); ea = f + 1
    return res

for name, pat in {
    "CTENFDAM": b"CTENFDAM",
    "neteasecloudmusic": b"neteasecloudmusic",
    "163key": b"163 key(Don't modify):",
    "A0": b"hzHRAmso5kInbaxW",
}.items():
    print(name, [hex(x) for x in find_all(pat)])

# list NCM-related functions
import idautils, ida_funcs
for ea in idautils.Functions():
    n = ida_funcs.get_func_name(ea) or ""
    if "CloundMusicAudioFile" in n or "NCM" in n:
        print(hex(ea), n)

# decompile
print("\n".join(db.pseudocode.decompile(0x1007cb76e).to_text()))
```
