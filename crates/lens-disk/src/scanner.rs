use std::collections::HashSet;
use std::fs;
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};

use crate::arena::{ArenaTree, DiskMetadata, FsKind, FsNode, NodeId};
use lens_core::{FileIdentity, LensError, Result};

#[derive(Debug, Clone)]
pub struct ScanOptions {
    pub max_depth: usize,
    pub max_entries: usize,
    pub one_file_system: bool,
    pub min_size: u64,
    pub exclude_patterns: Vec<String>,
}

impl Default for ScanOptions {
    fn default() -> Self {
        Self {
            max_depth: 512,
            max_entries: 1_000_000,
            one_file_system: false,
            min_size: 0,
            exclude_patterns: Vec::new(),
        }
    }
}

#[derive(Debug)]
pub struct ScanResult {
    pub tree: ArenaTree,
    pub root_id: NodeId,
    pub scanned_entries: usize,
    pub complete: bool,
    pub truncated: bool,
    pub errors: Vec<String>,
}

pub struct DiskScanner {
    options: ScanOptions,
}

impl DiskScanner {
    pub fn new(options: ScanOptions) -> Self {
        Self { options }
    }

    pub fn scan<P: AsRef<Path>>(&self, root_path: P) -> Result<ScanResult> {
        let root = root_path
            .as_ref()
            .canonicalize()
            .map_err(|e| LensError::Io {
                path: root_path.as_ref().to_path_buf(),
                source: e,
            })?;

        let mut tree = ArenaTree::with_capacity(4096);
        let mut errors = Vec::new();
        let mut visited_dirs: HashSet<(u64, u64)> = HashSet::new();

        let root_meta = match fs::symlink_metadata(&root) {
            Ok(m) => m,
            Err(e) => {
                return Err(LensError::Io {
                    path: root.clone(),
                    source: e,
                });
            }
        };

        let root_dev = root_meta.dev();
        let root_is_dir = root_meta.is_dir();
        let root_name = root
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_else(|| "/".to_string());

        let mut root_node = FsNode::new(root_name, ".".to_string(), root_is_dir);
        root_node.metadata = Self::extract_metadata(&root_meta);
        if !root_is_dir {
            root_node.size = root_meta.len();
            root_node.allocated_size = root_meta.blocks() * 512;
        }

        let root_id = tree.alloc(root_node);
        if root_is_dir {
            visited_dirs.insert((root_meta.dev(), root_meta.ino()));
        }

        let mut truncated = false;
        let mut scanned_entries = 1;

        if root_is_dir {
            let mut stack: Vec<(NodeId, PathBuf, usize)> = vec![(root_id, root.clone(), 0)];

            while let Some((parent_id, current_dir, depth)) = stack.pop() {
                if depth >= self.options.max_depth {
                    continue;
                }

                let read_dir = match fs::read_dir(&current_dir) {
                    Ok(rd) => rd,
                    Err(e) => {
                        errors.push(format!("Cannot read dir {:?}: {}", current_dir, e));
                        if let Some(p) = tree.get_mut(parent_id) {
                            p.complete = false;
                            p.error = e.to_string();
                        }
                        continue;
                    }
                };

                let mut entries = Vec::new();
                for entry_res in read_dir {
                    let entry = match entry_res {
                        Ok(e) => e,
                        Err(e) => {
                            errors.push(format!("Error during dir walk {:?}: {}", current_dir, e));
                            continue;
                        }
                    };
                    entries.push(entry);
                }

                // Deterministic sort by file name
                entries.sort_by_key(|e| e.file_name());

                for entry in entries {
                    if scanned_entries >= self.options.max_entries {
                        truncated = true;
                        break;
                    }

                    let path = entry.path();
                    let name = entry.file_name().to_string_lossy().into_owned();

                    // Check pattern exclusion
                    if self
                        .options
                        .exclude_patterns
                        .iter()
                        .any(|pat| name.contains(pat))
                    {
                        continue;
                    }

                    let sym_meta = match fs::symlink_metadata(&path) {
                        Ok(m) => m,
                        Err(e) => {
                            errors.push(format!("Cannot stat {:?}: {}", path, e));
                            continue;
                        }
                    };

                    let is_symlink = sym_meta.file_type().is_symlink();
                    let is_dir = sym_meta.is_dir();
                    let mut cycle_skipped = false;
                    let mut mount_boundary_skipped = false;
                    let mut target_meta: Option<DiskMetadata> = None;
                    let mut actual_is_dir = is_dir;

                    if is_symlink {
                        if let Ok(target_m) = fs::metadata(&path) {
                            actual_is_dir = target_m.is_dir();
                            target_meta = Some(Self::extract_metadata(&target_m));
                        }
                    }

                    if actual_is_dir {
                        if self.options.one_file_system && sym_meta.dev() != root_dev {
                            mount_boundary_skipped = true;
                        } else if !visited_dirs.insert((sym_meta.dev(), sym_meta.ino())) {
                            cycle_skipped = true;
                        }
                    }

                    let rel_path = path
                        .strip_prefix(&root)
                        .unwrap_or(&path)
                        .to_string_lossy()
                        .into_owned();
                    let mut node = FsNode::new(name, rel_path, actual_is_dir);
                    node.metadata = Self::extract_metadata(&sym_meta);
                    node.target_metadata = target_meta;
                    node.followed = is_symlink && node.target_metadata.is_some();
                    node.cycle_skipped = cycle_skipped;
                    node.mount_boundary_skipped = mount_boundary_skipped;

                    if !actual_is_dir {
                        node.size = sym_meta.len();
                        node.allocated_size = sym_meta.blocks() * 512;
                    }

                    let child_id = tree.alloc(node);
                    tree.add_child(parent_id, child_id);
                    scanned_entries += 1;

                    if actual_is_dir && !cycle_skipped && !mount_boundary_skipped && !is_symlink {
                        stack.push((child_id, path, depth + 1));
                    }
                }

                if truncated {
                    break;
                }
            }
        }

        // Post-order aggregation
        tree.aggregate_sizes(root_id);

        Ok(ScanResult {
            tree,
            root_id,
            scanned_entries,
            complete: errors.is_empty() && !truncated,
            truncated,
            errors,
        })
    }

    fn extract_metadata(meta: &fs::Metadata) -> DiskMetadata {
        let kind = if meta.file_type().is_symlink() {
            FsKind::Symlink
        } else if meta.is_dir() {
            FsKind::Directory
        } else if meta.is_file() {
            FsKind::RegularFile
        } else {
            FsKind::Other
        };

        DiskMetadata {
            kind,
            identity: FileIdentity::from_metadata(meta),
            logical_size: meta.len(),
            allocated_size: meta.blocks() * 512,
            hard_link_count: meta.nlink(),
            hard_link_count_known: true,
            permissions: meta.mode(),
            permissions_known: true,
            owner: meta.uid() as u64,
            group: meta.gid() as u64,
            ownership_known: true,
            modified_ns: meta.mtime_nsec(),
            modified_time_known: true,
        }
    }
}
