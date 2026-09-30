// Command rencm decrypts NetEase Cloud Music .ncm files.
//
//	rencm <file.ncm> [output-prefix]
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"rencm/ncm"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: rencm <file.ncm> [output-prefix]")
		os.Exit(2)
	}
	if err := run(os.Args[1], os.Args[2:]); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(input string, rest []string) error {
	dec, err := ncm.Open(input)
	if err != nil {
		return err
	}
	defer dec.Close()

	prefix := strings.TrimSuffix(input, filepath.Ext(input))
	if len(rest) > 0 {
		prefix = rest[0]
	}

	audioPath := prefix + "." + dec.Format().Ext()
	out, err := os.Create(audioPath)
	if err != nil {
		return err
	}
	hasher := sha256.New()
	n, err := dec.WriteTo(io.MultiWriter(out, hasher))
	if cerr := out.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return err
	}

	fmt.Printf("version   : %d\n", dec.Version())
	fmt.Printf("format    : %s\n", dec.Format())
	fmt.Printf("box_key   : %d bytes\n", len(dec.BoxKey()))
	fmt.Printf("audio     : %d bytes\n", n)
	fmt.Printf("sha256    : %s\n", hex.EncodeToString(hasher.Sum(nil)))
	fmt.Printf("output    : %s\n", audioPath)

	if cover := dec.Cover(); len(cover) > 0 {
		coverPath := prefix + ".jpg"
		if err := os.WriteFile(coverPath, cover, 0o644); err == nil {
			fmt.Printf("cover_out : %s\n", coverPath)
		}
	}
	// Print the metadata exactly as it appears in the file (raw bytes).
	// Using the parsed Metadata struct for display would depend on the JSON
	// encoding order; the raw payload is the faithful representation.
	if meta := dec.MetaJSON(); len(meta) > 0 {
		fmt.Printf("metadata  : %s\n", meta)
	}
	return nil
}
