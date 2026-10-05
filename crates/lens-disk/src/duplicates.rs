use std::collections::HashMap;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use crate::arena::ArenaTree;
use lens_core::{digest_bytes, digest_file, Result};

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DuplicateGroup {
    pub size: u64,
    pub sha256: String,
    pub files: Vec<PathBuf>,
    pub reclaimable_bytes: u64,
}

pub struct DuplicateFinder {
    min_size: u64,
}

impl DuplicateFinder {
    pub fn new(min_size: u64) -> Self {
        Self { min_size }
    }

    /// Finds duplicates across files registered in an ArenaTree.
    pub fn find_in_tree(&self, tree: &ArenaTree, base_dir: &Path) -> Result<Vec<DuplicateGroup>> {
        // Step 1: Bucket regular files by size. Symlinks are excluded: their
        // node size is the link's own, not the target's, so hashing them would
        // compare the wrong bytes. Hardlinks are folded by (dev, ino) using
        // the identity already captured during the scan — no extra stat, and
        // each inode is hashed at most once.
        use crate::arena::FsKind;

        let mut size_buckets: HashMap<u64, HashMap<(u64, u64), Vec<PathBuf>>> = HashMap::new();
        let mut file_count_by_size: HashMap<u64, usize> = HashMap::new();

        for node in &tree.nodes {
            if node.is_dir || node.metadata.kind == FsKind::Symlink || node.size < self.min_size {
                continue;
            }
            let id = (node.metadata.identity.device, node.metadata.identity.inode);
            let full_path = base_dir.join(&node.rel_path);
            *file_count_by_size.entry(node.size).or_default() += 1;
            size_buckets
                .entry(node.size)
                .or_default()
                .entry(id)
                .or_default()
                .push(full_path);
        }

        let mut groups = Vec::new();

        for (size, inode_buckets) in size_buckets {
            if file_count_by_size.get(&size).copied().unwrap_or(0) < 2 {
                continue;
            }

            // Step 2: Partial hash (first 4KB) once per unique inode.
            let mut partial_buckets: HashMap<String, Vec<(u64, u64)>> = HashMap::new();
            for (id, paths) in &inode_buckets {
                if let Ok(partial_hash) = Self::compute_partial_hash(&paths[0]) {
                    partial_buckets.entry(partial_hash).or_default().push(*id);
                }
            }

            // Step 3: Full SHA-256 for matching partial hashes.
            for (_p_hash, inode_ids) in partial_buckets {
                if inode_ids.len() < 2 {
                    continue;
                }

                let mut full_buckets: HashMap<String, Vec<(u64, u64)>> = HashMap::new();
                for id in inode_ids {
                    if let Ok(full_hash) = digest_file(&inode_buckets[&id][0]) {
                        full_buckets.entry(full_hash).or_default().push(id);
                    }
                }

                for (sha256, ids) in full_buckets {
                    if ids.len() < 2 {
                        continue;
                    }
                    let mut files: Vec<PathBuf> = ids
                        .iter()
                        .flat_map(|id| inode_buckets[id].iter().cloned())
                        .collect();
                    files.sort();
                    let distinct_copies = ids.len();
                    let reclaimable_bytes = if distinct_copies > 1 {
                        (distinct_copies as u64 - 1) * size
                    } else {
                        0
                    };

                    groups.push(DuplicateGroup {
                        size,
                        sha256,
                        files,
                        reclaimable_bytes,
                    });
                }
            }
        }

        // Sort descending by potential reclaimable bytes
        groups.sort_by_key(|b| std::cmp::Reverse(b.reclaimable_bytes));

        Ok(groups)
    }

    fn compute_partial_hash(path: &Path) -> std::io::Result<String> {
        let mut file = File::open(path)?;
        let mut buffer = [0u8; 4096];
        let bytes_read = file.read(&mut buffer)?;
        Ok(digest_bytes(&buffer[..bytes_read]))
    }
}
