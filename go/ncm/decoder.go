package ncm

import (
	"bufio"
	"encoding/binary"
	"io"
	"os"
)

// maxSectionSize bounds a single container section (key / comment / cover) so a
// crafted length field cannot drive a huge allocation. Real sections are tiny
// (key ~144 B, comment a few KB, cover a few MB).
const maxSectionSize = 64 << 20

// Decoder parses an NCM stream and exposes the decrypted audio as an io.Reader.
//
// The source reader is consumed sequentially: everything up to the audio
// section is read during construction, and the Decoder then streams the
// decrypted audio.
type Decoder struct {
	r      io.Reader
	closer io.Closer

	version  byte
	boxKey   []byte
	cover    []byte
	metaJSON []byte
	format   Format
	table    [256]byte
	pos      int
	prefix   []byte
}

// Open opens an NCM file from disk.
func Open(path string) (*Decoder, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	d, err := NewDecoder(f)
	if err != nil {
		_ = f.Close()
		return nil, err
	}
	d.closer = f
	return d, nil
}

// NewDecoder parses the container header from r and prepares streaming
// decryption of the audio section. r is wrapped in a small buffer.
func NewDecoder(r io.Reader) (*Decoder, error) {
	br := bufio.NewReader(r)
	d := &Decoder{r: br}

	var hdr [10]byte
	if _, err := io.ReadFull(br, hdr[:]); err != nil {
		return nil, err
	}
	if string(hdr[:8]) != magic {
		return nil, ErrBadMagic
	}
	d.version = hdr[8]

	keyBlob, err := readFrame(br)
	if err != nil {
		return nil, err
	}
	if d.boxKey, err = decryptKey(keyBlob); err != nil {
		return nil, err
	}

	comment, err := readFrame(br)
	if err != nil {
		return nil, err
	}
	// Metadata is optional: the audio does not depend on it, so a malformed
	// comment section must not reject an otherwise valid file.
	if len(comment) > 0 {
		if json, cerr := decryptComment(comment); cerr == nil {
			d.metaJSON = json
		}
	}

	if err := skip(br, 5); err != nil {
		return nil, err
	}
	coverSize, err := readLen(br)
	if err != nil {
		return nil, err
	}
	// The container repeats the cover size. The first field is authoritative
	// (it is where the audio starts); a larger duplicate would leave the audio
	// misaligned, so reject it instead of emitting silently wrong bytes.
	coverSizeDup, image, err := readFrameLen(br)
	if err != nil {
		return nil, err
	}
	if coverSize < coverSizeDup {
		return nil, ErrBadCover
	}
	if coverSize > coverSizeDup {
		if err := skip(br, int64(coverSize-coverSizeDup)); err != nil {
			return nil, err
		}
	}
	if coverSizeDup > 0 {
		d.cover = image
	}

	// Precompute the keystream and sniff the first 12 decrypted bytes.
	d.table = keystream(keyBox(d.boxKey))
	var pfx [12]byte
	n, err := readUpTo(br, pfx[:])
	if err != nil {
		return nil, err
	}
	d.prefix = pfx[:n]
	for i := range d.prefix {
		d.prefix[i] ^= d.table[i&0xFF]
	}
	d.pos = n
	d.format = detectFormat(d.prefix)
	return d, nil
}

// Read returns decrypted audio bytes. It implements io.Reader.
func (d *Decoder) Read(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	if len(d.prefix) > 0 {
		n := copy(p, d.prefix)
		d.prefix = d.prefix[n:]
		return n, nil
	}
	n, err := d.r.Read(p)
	for i := 0; i < n; i++ {
		p[i] ^= d.table[d.pos&0xFF]
		d.pos++
	}
	return n, err
}

// WriteTo copies the decrypted audio to w. It implements io.WriterTo.
func (d *Decoder) WriteTo(w io.Writer) (int64, error) {
	buf := make([]byte, 64*1024)
	var total int64
	for {
		n, err := d.Read(buf)
		if n > 0 {
			if m, werr := w.Write(buf[:n]); werr != nil {
				return total + int64(m), werr
			}
			total += int64(n)
		}
		if err == io.EOF {
			return total, nil
		}
		if err != nil {
			return total, err
		}
	}
}

// Close closes the underlying file if the Decoder was created with Open.
func (d *Decoder) Close() error {
	if d.closer != nil {
		return d.closer.Close()
	}
	return nil
}

// Version returns the container version byte.
func (d *Decoder) Version() byte { return d.version }

// Format returns the detected audio format.
func (d *Decoder) Format() Format { return d.format }

// BoxKey returns the derived box key (the "neteasecloudmusic"-stripped key).
func (d *Decoder) BoxKey() []byte { return d.boxKey }

// Cover returns the embedded cover image bytes, or nil if absent.
func (d *Decoder) Cover() []byte { return d.cover }

// MetaJSON returns the raw metadata JSON (without the "music:" prefix).
func (d *Decoder) MetaJSON() []byte { return d.metaJSON }

// Metadata parses the metadata JSON. It returns (nil, nil) when the file has
// no metadata section.
func (d *Decoder) Metadata() (*Metadata, error) {
	if len(d.metaJSON) == 0 {
		return nil, nil
	}
	return ParseMetadata(d.metaJSON)
}

func readLen(r io.Reader) (uint32, error) {
	var b [4]byte
	if _, err := io.ReadFull(r, b[:]); err != nil {
		return 0, err
	}
	return binary.LittleEndian.Uint32(b[:]), nil
}

func readFrameLen(r io.Reader) (uint32, []byte, error) {
	n, err := readLen(r)
	if err != nil {
		return 0, nil, err
	}
	if n == 0 {
		return 0, nil, nil
	}
	// n is attacker-controlled; cap it so a crafted length cannot force a huge
	// allocation before the short read is detected.
	if n > maxSectionSize {
		return n, nil, ErrSectionTooLarge
	}
	data := make([]byte, n)
	if _, err := io.ReadFull(r, data); err != nil {
		return n, nil, err
	}
	return n, data, nil
}

func readFrame(r io.Reader) ([]byte, error) {
	_, data, err := readFrameLen(r)
	return data, err
}

func skip(r io.Reader, n int64) error {
	if n <= 0 {
		return nil
	}
	_, err := io.CopyN(io.Discard, r, n)
	return err
}

// readUpTo fills buf, tolerating a short read at EOF.
func readUpTo(r io.Reader, buf []byte) (int, error) {
	n, err := io.ReadFull(r, buf)
	if err == io.ErrUnexpectedEOF || err == io.EOF {
		return n, nil
	}
	return n, err
}
