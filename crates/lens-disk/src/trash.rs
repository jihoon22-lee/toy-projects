use serde::Serialize;
use std::fs;
use std::path::{Component, Path, PathBuf};

use lens_core::{FileIdentity, LensError, Result};

#[derive(Debug, Clone, Serialize)]
pub struct TrashReceipt {
    pub original_path: PathBuf,
    pub trashed_file_path: PathBuf,
    pub info_path: PathBuf,
    pub deletion_date: String,
    pub size: u64,
    pub identity: FileIdentity,
}

/// Percent-encode a path per the FreeDesktop Trash spec (`Path=` is a
/// URI-style value: reserved bytes are `%XX` escaped). Unreserved +
/// `-._~/` are passed through.
fn encode_trashinfo_path(path: &Path) -> String {
    use std::os::unix::ffi::OsStrExt;
    let mut out = String::new();
    for &b in path.as_os_str().as_bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~' | b'/' => {
                out.push(b as char)
            }
            _ => out.push_str(&format!("%{:02X}", b)),
        }
    }
    out
}

fn identity_matches(a: &FileIdentity, b: &FileIdentity) -> bool {
    // ctime changes on rename, so it is deliberately excluded.
    a.device == b.device
        && a.inode == b.inode
        && a.mode == b.mode
        && a.size == b.size
        && a.modified_sec == b.modified_sec
        && a.modified_nsec == b.modified_nsec
}

fn occupied(path: &Path) -> bool {
    // lstat semantics: a dangling symlink still occupies the name.
    path.symlink_metadata().is_ok()
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
    /// Symlinks are moved as links — the target is never touched.
    pub fn move_to_trash<P: AsRef<Path>>(&self, path: P) -> Result<TrashReceipt> {
        self.ensure_directories()?;
        // Absolutize without resolving the final component: canonicalize()
        // would turn `link -> /target` into `/target` and trash the target.
        let original = std::path::absolute(path.as_ref()).map_err(|e| LensError::Io {
            path: path.as_ref().to_path_buf(),
            source: e,
        })?;

        let meta = fs::symlink_metadata(&original).map_err(|e| LensError::Io {
            path: original.clone(),
            source: e,
        })?;
        let identity = FileIdentity::from_metadata(&meta);
        let file_name = original
            .file_name()
            .ok_or_else(|| LensError::InvalidInput {
                message: "Cannot trash filesystem root".to_string(),
            })?
            .to_string_lossy();

        // Generate a unique destination name in trash (lstat checks, so a
        // dangling symlink still counts as occupied).
        let mut candidate_name = file_name.to_string();
        let mut counter = 1;
        while occupied(&self.files_dir().join(&candidate_name))
            || occupied(
                &self
                    .info_dir()
                    .join(format!("{}.trashinfo", candidate_name)),
            )
        {
            candidate_name = format!("{}_{}", file_name, counter);
            counter += 1;
        }

        let trashed_file_path = self.files_dir().join(&candidate_name);
        let info_path = self
            .info_dir()
            .join(format!("{}.trashinfo", candidate_name));

        // FreeDesktop `DeletionDate`: local time in `YYYY-MM-DDTHH:MM:SS`.
        let deletion_date = lens_core::time::local_now_iso();

        // 1. Write the .trashinfo file (create_new: never follow a planted
        // symlink or clobber an existing entry).
        let info_content = format!(
            "[Trash Info]\nPath={}\nDeletionDate={}\n",
            encode_trashinfo_path(&original),
            deletion_date
        );
        {
            use std::io::Write;
            let mut info_file = fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&info_path)
                .map_err(|e| LensError::Io {
                    path: info_path.clone(),
                    source: e,
                })?;
            info_file
                .write_all(info_content.as_bytes())
                .map_err(|e| LensError::Io {
                    path: info_path.clone(),
                    source: e,
                })?;
        }

        // 2. Move the file into files/. `rename` on a symlink moves the
        // link itself. (Note: POSIX rename is replace-on-race; the lstat
        // check above narrows but does not eliminate that window.)
        if let Err(e) = fs::rename(&original, &trashed_file_path) {
            // Attempt clean rollback of info file if rename fails
            let _ = fs::remove_file(&info_path);
            return Err(LensError::Io {
                path: original,
                source: e,
            });
        }

        Ok(TrashReceipt {
            original_path: original,
            trashed_file_path,
            info_path,
            deletion_date,
            size: identity.size,
            identity,
        })
    }

    /// Restores a trashed file back to its original location.
    pub fn restore(&self, receipt: &TrashReceipt) -> Result<()> {
        // Receipts must reference a file inside this trash's files/ dir and
        // a clean absolute original path.
        if !receipt.trashed_file_path.starts_with(self.files_dir()) {
            return Err(LensError::InvalidInput {
                message: format!(
                    "Trashed path {:?} is outside {}",
                    receipt.trashed_file_path,
                    self.files_dir().display()
                ),
            });
        }
        if !receipt.original_path.is_absolute()
            || receipt
                .original_path
                .components()
                .any(|c| matches!(c, Component::ParentDir))
        {
            return Err(LensError::InvalidInput {
                message: format!("Refusing unsafe restore path {:?}", receipt.original_path),
            });
        }

        // Verify the file in trash is still the file that was trashed.
        let current =
            fs::symlink_metadata(&receipt.trashed_file_path).map_err(|e| LensError::Io {
                path: receipt.trashed_file_path.clone(),
                source: e,
            })?;
        if !identity_matches(&FileIdentity::from_metadata(&current), &receipt.identity) {
            return Err(LensError::InvalidInput {
                message: format!(
                    "Trashed file {:?} no longer matches its recorded identity",
                    receipt.trashed_file_path
                ),
            });
        }

        if occupied(&receipt.original_path) {
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
