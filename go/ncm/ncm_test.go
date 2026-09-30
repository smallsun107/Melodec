package ncm

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"testing"
)

func TestDetectFormat(t *testing.T) {
	cases := []struct {
		name string
		b    []byte
		want Format
	}{
		{"flac", []byte("fLaC\x00\x00\x00\x22"), FormatFLAC},
		{"ogg", []byte("OggS\x00\x02"), FormatOgg},
		{"id3", []byte("ID3\x04\x00"), FormatMP3},
		{"mp3 mpeg1 layer3", []byte{0xFF, 0xFB, 0x90}, FormatMP3},
		{"mp3 mpeg2.5 layer3", []byte{0xFF, 0xE3, 0x00}, FormatMP3},
		{"aac adts mpeg4", []byte{0xFF, 0xF1, 0x50}, FormatAAC},
		{"aac adts mpeg2", []byte{0xFF, 0xF9, 0x50}, FormatAAC},
		{"m4a brand M4A ", ftyp("M4A "), FormatM4A},
		{"m4a brand mp42", ftyp("mp42"), FormatM4A},
		{"m4a brand isom", ftyp("isom"), FormatM4A},
		{"garbage", []byte("not-a-known-header"), FormatUnknown},
		{"short", []byte{0xFF}, FormatUnknown},
		{"empty", nil, FormatUnknown},
	}
	for _, c := range cases {
		if got := detectFormat(c.b); got != c.want {
			t.Errorf("detectFormat(%s) = %q, want %q", c.name, got, c.want)
		}
	}
}

// ftyp builds the 12-byte MPEG-4 file-type box prefix for a brand.
func ftyp(brand string) []byte {
	b := make([]byte, 12)
	copy(b[4:8], "ftyp")
	copy(b[8:12], brand)
	return b
}

func TestTrimASCIIWS(t *testing.T) {
	cases := []struct{ in, want string }{
		{"  abc\t", "abc"},
		{"\r\nabc\n", "abc"},
		{"abc", "abc"},
		{"   ", ""},
		{"", ""},
		{"a b", "a b"},
		{"\xa0abc\xa0", "\xa0abc\xa0"}, // U+00A0 halves are not ASCII whitespace
	}
	for _, c := range cases {
		if got := string(trimASCIIWS([]byte(c.in))); got != c.want {
			t.Errorf("trimASCIIWS(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestPKCS7Unpad(t *testing.T) {
	cases := []struct {
		name string
		in   []byte
		want []byte
	}{
		{"valid pad 3", []byte{1, 2, 3, 3, 3, 3}, []byte{1, 2, 3}},
		{"valid pad 1", []byte{9, 1}, []byte{9}},
		{"bad run", []byte{1, 2, 3, 3, 4, 3}, []byte{1, 2, 3, 3, 4, 3}},
		{"pad too big", []byte{1, 2}, []byte{1, 2}}, // last byte 2 > len? no: 2 <= len, run ok
		{"zero pad", []byte{1, 2, 0}, []byte{1, 2, 0}},
		{"empty", nil, nil},
	}
	for _, c := range cases {
		got := pkcs7Unpad(append([]byte(nil), c.in...))
		if !bytes.Equal(got, c.want) {
			t.Errorf("pkcs7Unpad(%s) = %v, want %v", c.name, got, c.want)
		}
	}
}

// TestGoldenSample decodes a real .ncm and checks the vectors documented in the
// README. It is skipped unless RENCM_SAMPLE points at a sample file.
func TestGoldenSample(t *testing.T) {
	path := os.Getenv("RENCM_SAMPLE")
	if path == "" {
		t.Skip("set RENCM_SAMPLE to a .ncm file to run the golden decode")
	}

	dec, err := Open(path)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	defer dec.Close()

	if dec.Version() != 1 {
		t.Errorf("version = %d, want 1", dec.Version())
	}
	if dec.Format() != FormatMP3 {
		t.Errorf("format = %q, want mp3", dec.Format())
	}
	if got := len(dec.BoxKey()); got != 112 {
		t.Errorf("box_key = %d bytes, want 112", got)
	}
	if got := len(dec.Cover()); got != 38969 {
		t.Errorf("cover = %d bytes, want 38969", got)
	}
	if got := sha(dec.BoxKey()); got != "7e07a6ecb23fd345ef8fe390998d4496e951edaad52dc06b0c866ba7174ee685" {
		t.Errorf("box_key sha256 = %s", got)
	}
	if got := sha(dec.Cover()); got != "05a4de5aafc664c9843a7f808e17185d5eff0f7b88969f32af1fa44c8651e71f" {
		t.Errorf("cover sha256 = %s", got)
	}

	var audio bytes.Buffer
	if _, err := dec.WriteTo(&audio); err != nil {
		t.Fatalf("write: %v", err)
	}
	if got := audio.Len(); got != 7448076 {
		t.Errorf("audio = %d bytes, want 7448076", got)
	}
	if got := sha(audio.Bytes()); got != "ea30a900af1843433ff24459c0351b7076508d489296245ac1c2c9865c9d0026" {
		t.Errorf("audio sha256 = %s", got)
	}
}

func sha(b []byte) string {
	sum := sha256.Sum256(b)
	return hex.EncodeToString(sum[:])
}
