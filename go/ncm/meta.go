package ncm

import (
	"encoding/json"
	"strconv"
	"time"
)

// Artist is one entry of the metadata "artist" array.
type Artist struct {
	Name string
	ID   int64
}

// Metadata is the parsed metadata JSON ("music:{...}").
type Metadata struct {
	MusicID    int64
	MusicName  string
	Artists    []Artist
	Album      string
	AlbumID    int64
	Bitrate    int
	DurationMS int64
	Format     string
	Raw        json.RawMessage
}

// Duration returns the track duration as a time.Duration.
func (m *Metadata) Duration() time.Duration {
	return time.Duration(m.DurationMS) * time.Millisecond
}

// ParseMetadata decodes the metadata JSON.
func ParseMetadata(data []byte) (*Metadata, error) {
	var wire struct {
		MusicID   json.Number         `json:"musicId"`
		MusicName string              `json:"musicName"`
		Artist    [][]json.RawMessage `json:"artist"`
		Album     string              `json:"album"`
		AlbumID   json.Number         `json:"albumId"`
		Bitrate   int                 `json:"bitrate"`
		Duration  int64               `json:"duration"`
		Format    string              `json:"format"`
	}
	if err := json.Unmarshal(data, &wire); err != nil {
		return nil, err
	}

	m := &Metadata{
		MusicID:    toInt64(string(wire.MusicID)),
		MusicName:  wire.MusicName,
		Album:      wire.Album,
		AlbumID:    toInt64(string(wire.AlbumID)),
		Bitrate:    wire.Bitrate,
		DurationMS: wire.Duration,
		Format:     wire.Format,
		Raw:        append(json.RawMessage(nil), data...),
	}
	for _, pair := range wire.Artist {
		if len(pair) == 0 {
			continue
		}
		a := Artist{Name: rawString(pair[0])}
		if len(pair) > 1 {
			a.ID = toInt64(rawString(pair[1]))
		}
		m.Artists = append(m.Artists, a)
	}
	return m, nil
}

// rawString decodes a JSON string; numbers fall back to their literal text.
func rawString(r json.RawMessage) string {
	var s string
	if err := json.Unmarshal(r, &s); err == nil {
		return s
	}
	return string(r)
}

func toInt64(s string) int64 {
	n, _ := strconv.ParseInt(s, 10, 64)
	return n
}
