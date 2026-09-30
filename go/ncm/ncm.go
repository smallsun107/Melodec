// Package ncm implements a streaming decoder for NetEase Cloud Music `.ncm`
// files.
//
// The format and algorithms were recovered by reverse engineering the
// NeteaseMusic macOS binary. See docs/NCM-FORMAT-ANALYSIS.md for the full write-up and
// the IDA addresses behind each step.
//
// Typical streaming usage:
//
//	dec, err := ncm.Open("song.ncm")
//	if err != nil {
//		log.Fatal(err)
//	}
//	defer dec.Close()
//
//	out, _ := os.Create("song." + dec.Format().Ext())
//	defer out.Close()
//	if _, err := dec.WriteTo(out); err != nil {
//		log.Fatal(err)
//	}
//	if meta, _ := dec.Metadata(); meta != nil {
//		fmt.Println(meta.MusicName, meta.Artists)
//	}
//
// The pipeline is:
//
//	key_blob --XOR 0x64--> AES-128-ECB(core key) --> PKCS#7 unpad
//	         --> strip "neteasecloudmusic"  ==> box key
//	box   = RC4-KSA(box key)
//	table = precomputed 256-byte NCM keystream, cycled over the audio
//	audio[i] ^= table[i % 256]
//	comment --XOR 0x63--> base64 --> AES-128-ECB(meta key) --> JSON
package ncm

// magic is the fixed 8-byte header of every NCM container.
const magic = "CTENFDAM"

// Format identifies the decoded audio container.
type Format string

const (
	FormatMP3     Format = "mp3"
	FormatFLAC    Format = "flac"
	FormatM4A     Format = "m4a"
	FormatOgg     Format = "ogg"
	FormatAAC     Format = "aac"
	FormatUnknown Format = "bin"
)

// Ext returns the usual file extension (without a leading dot).
func (f Format) Ext() string { return string(f) }

func (f Format) String() string { return string(f) }

// detectFormat sniffs the decoded audio header.
//
// Both MPEG families start with an 11-bit 0xFFE sync, so they are told apart by
// the two layer bits: ADTS (AAC) uses layer 00, MPEG audio (mp3) layer 01/10/11.
func detectFormat(b []byte) Format {
	switch {
	case len(b) >= 4 && string(b[:4]) == "fLaC":
		return FormatFLAC
	case len(b) >= 4 && string(b[:4]) == "OggS":
		return FormatOgg
	case len(b) >= 3 && string(b[:3]) == "ID3":
		return FormatMP3
	case len(b) >= 2 && b[0] == 0xFF && b[1]&0xE0 == 0xE0 && b[1]&0x06 != 0:
		return FormatMP3 // MPEG-1/2/2.5 Layer I/II/III
	case len(b) >= 2 && b[0] == 0xFF && b[1]&0xF6 == 0xF0:
		return FormatAAC // ADTS (MPEG-4 / MPEG-2)
	case len(b) >= 12 && string(b[4:8]) == "ftyp" && isM4ABrand(b[8:12]):
		return FormatM4A
	}
	return FormatUnknown
}

// isM4ABrand reports whether a 4-byte ftyp brand denotes an MPEG-4 audio file.
func isM4ABrand(brand []byte) bool {
	switch string(brand) {
	case "M4A ", "M4B ", "mp41", "mp42", "isom":
		return true
	}
	return false
}
