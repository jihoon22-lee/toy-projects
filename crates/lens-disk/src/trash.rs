use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

use lens_core::{FileIdentity, LensError, Result};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TrashReceipt {
    pub original_path: PathBuf,
    pub trashed_file_path: PathBuf,
    pub info_path: PathBuf,
    pub deletion_date: String,
    pub size: u64,
    pub identity: FileIdentity,
}

pub struct TrashManager {
    trash_dir: PathBuf,
}

impl Default for TrashManager {
    fn default() -> Self {
        let base = std::env::var("XDG_DATA_HOME")
            .map(PathBuf::from)
            .unwrap_or_else(|_| {
                let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
                PathBuf::from(home).join(".local/share")
            });
        Self {
            trash_dir: base.join("Trash"),
        }
    }
}

impl TrashManager {
    pub fn new(trash_dir: PathBuf) -> Self {
        Self { trash_dir }
    }

    pub fn files_dir(&self) -> PathBuf {
        self.trash_dir.join("files")
    }

    pub fn info_dir(&self) -> PathBuf {
        self.trash_dir.join("info")
    }

    pub fn ensure_directories(&self) -> Result<()> {
        fs::create_dir_all(self.files_dir()).map_err(|e| LensError::Io {
            path: self.files_dir(),
            source: e,
        })?;
        fs::create_dir_all(self.info_dir()).map_err(|e| LensError::Io {
            path: self.info_dir(),
            source: e,
        })?;
        Ok(())
    }

    /// Moves a file to trash following the FreeDesktop Trash Specification.
    pub fn move_to_trash<P: AsRef<Path>>(&self, path: P) -> Result<TrashReceipt> {
        self.ensure_directories()?;
        let canonical_path = path.as_ref().canonicalize().map_err(|e| LensError::Io {
            path: path.as_ref().to_path_buf(),
            source: e,
        })?;

        let identity = FileIdentity::from_path(&canonical_path)?;
        let file_name = canonical_path
            .file_name()
            .ok_or_else(|| LensError::InvalidInput {
                message: "Cannot trash filesystem root".to_string(),
            })?
            .to_string_lossy();

        // Generate a unique destination name in trash
        let mut candidate_name = file_name.to_string();
        let mut counter = 1;
        while self.files_dir().join(&candidate_name).exists()
            || self
                .info_dir()
                .join(format!("{}.trashinfo", candidate_name))
                .exists()
        {
            candidate_name = format!("{}_{}", file_name, counter);
            counter += 1;
        }

        let trashed_file_path = self.files_dir().join(&candidate_name);
        let info_path = self
            .info_dir()
            .join(format!("{}.trashinfo", candidate_name));

        // Format ISO-8601 timestamp
        let now = std::time::SystemTime::now();
        let deletion_date = humantime_or_iso(now);

        // 1. Write the .trashinfo file
        let info_content = format!(
            "[Trash Info]\nPath={}\nDeletionDate={}\n",
            canonical_path.to_string_lossy(),
            deletion_date
        );
        fs::write(&info_path, info_content).map_err(|e| LensError::Io {
            path: info_path.clone(),
            source: e,
        })?;

        // 2. Move the file into files/
        if let Err(e) = fs::rename(&canonical_path, &trashed_file_path) {
            // Attempt clean rollback of info file if rename fails
            let _ = fs::remove_file(&info_path);
            return Err(LensError::Io {
                path: canonical_path,
                source: e,
            });
        }

        Ok(TrashReceipt {
            original_path: canonical_path,
            trashed_file_path,
            info_path,
            deletion_date,
            size: identity.size,
            identity,
        })
    }

    /// Restores a trashed file back to its original location.
    pub fn restore(&self, receipt: &TrashReceipt) -> Result<()> {
        if receipt.original_path.exists() {
            return Err(LensError::InvalidInput {
                message: format!(
                    "Destination {:?} already exists; refusing to overwrite",
                    receipt.original_path
                ),
            });
        }

        if let Some(parent) = receipt.original_path.parent() {
            fs::create_dir_all(parent).map_err(|e| LensError::Io {
                path: parent.to_path_buf(),
                source: e,
            })?;
        }

        fs::rename(&receipt.trashed_file_path, &receipt.original_path).map_err(|e| {
            LensError::Io {
                path: receipt.trashed_file_path.clone(),
                source: e,
            }
        })?;

        // Remove the .trashinfo
        let _ = fs::remove_file(&receipt.info_path);

        Ok(())
    }
}

fn humantime_or_iso(now: std::time::SystemTime) -> String {
    let dur = now
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default();
    let secs = dur.as_secs();
    // Simple basic UTC calendar formatting
    let days = secs / 86400;
    let day_secs = secs % 86400;
    let hours = day_secs / 3600;
    let minutes = (day_secs % 3600) / 60;
    let seconds = day_secs % 60;

    // Approximate year/month from days since 1970
    let mut y = 1970;
    let mut d = days;
    loop {
        let leap = if y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) {
            366
        } else {
            365
        };
        if d < leap {
            break;
        }
        d -= leap;
        y += 1;
    }
    let leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    let month_days = [
        31,
        if leap { 29 } else { 28 },
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
    ];
    let mut m = 1;
    for &md in &month_days {
        if d < md {
            break;
        }
        d -= md;
        m += 1;
    }
    format!(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}",
        y,
        m,
        d + 1,
        hours,
        minutes,
        seconds
    )
}
