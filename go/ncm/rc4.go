package ncm

// keyBox builds the 256-byte RC4 state box from the box key (KSA).
func keyBox(key []byte) [256]byte {
	var s [256]byte
	for i := range s {
		s[i] = byte(i)
	}
	j := 0
	kp := 0
	for i := 0; i < 256; i++ {
		j = (j + int(s[i]) + int(key[kp])) & 0xFF
		kp = (kp + 1) % len(key)
		s[i], s[j] = s[j], s[i]
	}
	return s
}

// keystream precomputes the 256-byte NCM keystream table.
//
// For each data index i (0..255):
//
//	j     = i + 1
//	k2    = j + box[j]
//	table = box[box[j] + box[k2]]
//
// Because j only depends on i mod 256, cycling this table gives the same
// result as computing the keystream per byte, but with no per-byte branches.
func keystream(s [256]byte) [256]byte {
	var t [256]byte
	for i := 0; i < 256; i++ {
		j := byte(i + 1)                    // wraps to 0 when i == 255
		k2 := byte(int(j) + int(s[j]))      // (j + box[j]) & 0xFF
		idx := byte(int(s[j]) + int(s[k2])) // (box[j] + box[k2]) & 0xFF
		t[i] = s[idx]
	}
	return t
}
