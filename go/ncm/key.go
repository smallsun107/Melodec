package ncm

import (
	"bytes"
	"crypto/aes"
	"encoding/base64"
	"errors"
)

const (
	keyPrefix  = "neteasecloudmusic"
	coreAESKey = "hzHRAmso5kInbaxW"
	metaAESKey = "#14ljk_!\\]&0U<'("
	metaMarker = "163 key(Don't modify):"
	metaPrefix = "music:"
)

// Sentinel errors returned by the decoder.
var (
	ErrBadMagic        = errors.New("ncm: bad CTENFDAM magic")
	ErrBadKey          = errors.New("ncm: invalid key section")
	ErrBadComment      = errors.New("ncm: invalid comment section")
	ErrBadMeta         = errors.New("ncm: invalid metadata section")
	ErrBadCover        = errors.New("ncm: inconsistent cover size")
	ErrSectionTooLarge = errors.New("ncm: section length exceeds limit")
)

// trimASCIIWS trims ASCII whitespace. The C++ and Rust front ends do the same;
// bytes.TrimSpace would additionally strip bytes such as U+00A0 and diverge.
func trimASCIIWS(b []byte) []byte {
	isWS := func(c byte) bool { return c == ' ' || c == '\t' || c == '\r' || c == '\n' }
	i := 0
	for i < len(b) && isWS(b[i]) {
		i++
	}
	j := len(b)
	for j > i && isWS(b[j-1]) {
		j--
	}
	return b[i:j]
}

// aesECBDecrypt decrypts whole 16-byte blocks with AES-128-ECB.
// Any trailing partial block is ignored (NCM sections are block aligned).
func aesECBDecrypt(data, key []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	bs := block.BlockSize()
	n := len(data) - len(data)%bs
	out := make([]byte, n)
	for i := 0; i < n; i += bs {
		block.Decrypt(out[i:i+bs], data[i:i+bs])
	}
	return out, nil
}

// pkcs7Unpad removes PKCS#7 padding when valid, otherwise returns d unchanged.
func pkcs7Unpad(d []byte) []byte {
	n := len(d)
	if n == 0 {
		return d
	}
	pad := int(d[n-1])
	if pad < 1 || pad > 16 || pad > n {
		return d
	}
	for _, b := range d[n-pad:] {
		if int(b) != pad {
			return d
		}
	}
	return d[:n-pad]
}

// decryptKey turns the raw key section into the box key:
//
//	XOR 0x64 -> AES-128-ECB(core key) -> PKCS#7 unpad -> strip prefix
func decryptKey(blob []byte) ([]byte, error) {
	xored := make([]byte, len(blob))
	for i, b := range blob {
		xored[i] = b ^ 0x64
	}
	plain, err := aesECBDecrypt(xored, []byte(coreAESKey))
	if err != nil {
		return nil, err
	}
	plain = pkcs7Unpad(plain)
	if !bytes.HasPrefix(plain, []byte(keyPrefix)) {
		return nil, ErrBadKey
	}
	key := plain[len(keyPrefix):]
	if len(key) == 0 {
		return nil, ErrBadKey
	}
	return append([]byte(nil), key...), nil
}

// decryptComment turns the raw comment section into the metadata JSON:
//
//	XOR 0x63 -> strip marker -> base64 -> AES-128-ECB(meta key) -> strip "music:"
func decryptComment(comment []byte) ([]byte, error) {
	xored := make([]byte, len(comment))
	for i, b := range comment {
		xored[i] = b ^ 0x63
	}
	if !bytes.HasPrefix(xored, []byte(metaMarker)) {
		return nil, ErrBadComment
	}
	seg := xored[len(metaMarker):]
	if i := bytes.IndexByte(seg, 0); i >= 0 {
		seg = seg[:i] // the payload is NUL-padded
	}
	seg = trimASCIIWS(seg)
	if pad := len(seg) % 4; pad != 0 {
		seg = append(seg, bytes.Repeat([]byte{'='}, 4-pad)...)
	}
	raw, err := base64.StdEncoding.DecodeString(string(seg))
	if err != nil {
		return nil, err
	}
	plain, err := aesECBDecrypt(raw, []byte(metaAESKey))
	if err != nil {
		return nil, err
	}
	plain = pkcs7Unpad(plain)
	if !bytes.HasPrefix(plain, []byte(metaPrefix)) {
		return nil, ErrBadMeta
	}
	return append([]byte(nil), plain[len(metaPrefix):]...), nil
}
