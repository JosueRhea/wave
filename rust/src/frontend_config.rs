//! Front-end-only settings, kept separately from `WaveConfig`.
//!
//! `wave_config_save()` rewrites `~/.config/wave/config` from a fixed list of
//! fields, so any key it does not know about is destroyed on the next save.
//! Anything this front-end needs that the C core has no concept of — the font
//! family being the first — therefore lives in its own file beside it.

use std::path::PathBuf;

/// Bundled with the binary, so this is always available.
pub const DEFAULT_FONT: &str = "Geist Mono";

#[derive(Clone, Debug)]
pub struct FrontendConfig {
    pub font: String,
}

impl Default for FrontendConfig {
    fn default() -> Self {
        FrontendConfig {
            font: DEFAULT_FONT.to_string(),
        }
    }
}

fn path() -> Option<PathBuf> {
    let home = std::env::var_os("HOME")?;
    Some(PathBuf::from(home).join(".config/wave/gpui.conf"))
}

impl FrontendConfig {
    pub fn load() -> Self {
        let mut cfg = FrontendConfig::default();
        let Some(p) = path() else { return cfg };
        let Ok(text) = std::fs::read_to_string(p) else {
            return cfg;
        };
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some((key, value)) = line.split_once('=') {
                if key.trim() == "font" {
                    let value = value.trim();
                    if !value.is_empty() {
                        cfg.font = value.to_string();
                    }
                }
            }
        }
        cfg
    }

    pub fn save(&self) -> bool {
        let Some(p) = path() else { return false };
        if let Some(dir) = p.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        std::fs::write(&p, format!("# wave gpui front-end\nfont={}\n", self.font)).is_ok()
    }
}
