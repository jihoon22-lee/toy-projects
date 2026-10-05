use serde::{Deserialize, Serialize};
use std::fs::File;
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};

use crate::error::{LensError, Result};

/// POSIX file system identity to detect file modifications and hard link sharing.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct FileIdentity {
    pub device: u64,
    pub inode: u64,
    pub mode: u32,
    pub size: u64,
    pub modified_sec: i64,
    pub modified_nsec: i64,
    pub changed_sec: i64,
    pub changed_nsec: i64,
}

impl FileIdentity {
    pub fn from_metadata(meta: &std::fs::Metadata) -> Self {
        Self {
            device: meta.dev(),
            inode: meta.ino(),
            mode: meta.mode(),
            size: meta.size(),
            modified_sec: meta.mtime(),
            modified_nsec: meta.mtime_nsec(),
            changed_sec: meta.ctime(),
            changed_nsec: meta.ctime_nsec(),
        }
    }

    pub fn from_path<P: AsRef<Path>>(path: P) -> Result<Self> {
        let meta = std::fs::symlink_metadata(path.as_ref()).map_err(|e| LensError::Io {
            path: path.as_ref().to_path_buf(),
            source: e,
        })?;
        Ok(Self::from_metadata(&meta))
    }

    pub fn from_file(file: &File, path: &Path) -> Result<Self> {
        let meta = file.metadata().map_err(|e| LensError::Io {
            path: path.to_path_buf(),
            source: e,
        })?;
        Ok(Self::from_metadata(&meta))
    }

    pub fn is_same_file(&self, other: &Self) -> bool {
        self.device == other.device && self.inode == other.inode
    }

    pub fn is_unchanged(&self, current: &Self) -> bool {
        self == current
    }
}

/// A securely opened file with identity tracking against TOCTOU race conditions.
pub struct SafeInput {
    pub path: PathBuf,
    pub file: File,
    pub initial_identity: FileIdentity,
}

impl SafeInput {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path_buf = path.as_ref().to_path_buf();
        let file = File::open(&path_buf).map_err(|e| LensError::Io {
            path: path_buf.clone(),
            source: e,
        })?;
        let initial_identity = FileIdentity::from_file(&file, &path_buf)?;

        // Verify it is a regular file
        if (initial_identity.mode & 0o170000) != 0o100000 {
            return Err(LensError::InvalidInput {
                message: format!("Target {:?} is not a regular file", path_buf),
            });
        }

        Ok(Self {
            path: path_buf,
            file,
            initial_identity,
        })
    }

    /// Verifies that the file content and metadata on disk have not been altered.
    pub fn verify_unchanged(&self) -> Result<()> {
        let current_file_meta = FileIdentity::from_file(&self.file, &self.path)?;
        if !self.initial_identity.is_unchanged(&current_file_meta) {
            return Err(LensError::InputChanged {
                path: self.path.clone(),
            });
        }
        let current_path_meta = FileIdentity::from_path(&self.path)?;
        if !self.initial_identity.is_unchanged(&current_path_meta) {
            return Err(LensError::InputChanged {
                path: self.path.clone(),
            });
        }
        Ok(())
    }

    /// Safely maps the file to memory.
    pub fn mmap(&self) -> Result<memmap2::Mmap> {
        self.verify_unchanged()?;
        // SAFETY: The file is verified to be a regular file, opened read-only, and unchanged.
        unsafe {
            memmap2::MmapOptions::new()
                .map(&self.file)
                .map_err(|e| LensError::Io {
                    path: self.path.clone(),
                    source: e,
                })
        }
    }
}
