use serde_json::Value;

use crate::error::Result;

/// One entry of the metadata `artist` array.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Artist {
    pub name: String,
    pub id: Option<i64>,
}

/// Parsed metadata JSON (`music:{...}`).
#[derive(Debug, Clone)]
pub struct Metadata {
    value: Value,
}

impl Metadata {
    pub(crate) fn parse(bytes: &[u8]) -> Result<Self> {
        Ok(Metadata {
            value: serde_json::from_slice(bytes)?,
        })
    }

    /// The parsed JSON value.
    ///
    /// Note: this is a *parsed* view. `serde_json` stores objects in a
    /// `BTreeMap`, so object keys come back sorted and duplicate keys are
    /// collapsed. To show or forward the original payload, use
    /// `Decoder::meta_json()` instead.
    pub fn raw(&self) -> &Value {
        &self.value
    }

    pub fn music_id(&self) -> Option<i64> {
        self.value.get("musicId").and_then(as_i64)
    }

    pub fn music_name(&self) -> Option<&str> {
        self.value.get("musicName").and_then(Value::as_str)
    }

    pub fn album(&self) -> Option<&str> {
        self.value.get("album").and_then(Value::as_str)
    }

    pub fn album_id(&self) -> Option<i64> {
        self.value.get("albumId").and_then(as_i64)
    }

    pub fn bitrate(&self) -> Option<i64> {
        self.value.get("bitrate").and_then(as_i64)
    }

    pub fn duration_ms(&self) -> Option<i64> {
        self.value.get("duration").and_then(as_i64)
    }

    pub fn format(&self) -> Option<&str> {
        self.value.get("format").and_then(Value::as_str)
    }

    /// Artists as `(name, id)` pairs. The `id` may be missing.
    pub fn artists(&self) -> Vec<Artist> {
        self.value
            .get("artist")
            .and_then(Value::as_array)
            .map(|arr| {
                arr.iter()
                    .filter_map(|pair| {
                        let a = pair.as_array()?;
                        let name = a.first()?.as_str()?.to_string();
                        let id = a.get(1).and_then(as_i64);
                        Some(Artist { name, id })
                    })
                    .collect()
            })
            .unwrap_or_default()
    }
}

/// Accept either a JSON number or a numeric string.
fn as_i64(v: &Value) -> Option<i64> {
    v.as_i64()
        .or_else(|| v.as_str().and_then(|s| s.parse::<i64>().ok()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ids_accept_number_or_string() {
        assert_eq!(as_i64(&serde_json::json!(42)), Some(42));
        assert_eq!(as_i64(&serde_json::json!("42")), Some(42));
        assert_eq!(as_i64(&serde_json::json!("nope")), None);
    }
}
