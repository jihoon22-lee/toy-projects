use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use crate::arena::ArenaTree;
use lens_core::{digest_bytes, digest_file, FileIdentity, Result};

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
        // Step 1: Bucket files by size
        let mut size_buckets: HashMap<u64, Vec<PathBuf>> = HashMap::new();

        for node in &tree.nodes {
            if !node.is_dir && node.size >= self.min_size {
                let full_path = base_dir.join(&node.rel_path);
                size_buckets.entry(node.size).or_default().push(full_path);
            }
        }

        let mut groups = Vec::new();

        // Step 2: Partial hash (first 4KB) for size buckets with >= 2 files
        for (size, candidates) in size_buckets {
            if candidates.len() < 2 {
                continue;
            }

            let mut partial_buckets: HashMap<String, Vec<PathBuf>> = HashMap::new();

            for path in candidates {
                if let Ok(partial_hash) = Self::compute_partial_hash(&path) {
                    partial_buckets.entry(partial_hash).or_default().push(path);
                }
            }

            // Step 3: Full SHA-256 for matching partial hashes
            for (_p_hash, full_candidates) in partial_buckets {
                if full_candidates.len() < 2 {
                    continue;
                }

                let mut full_buckets: HashMap<String, Vec<PathBuf>> = HashMap::new();
                for path in full_candidates {
                    if let Ok(full_hash) = digest_file(&path) {
                        full_buckets.entry(full_hash).or_default().push(path);
                    }
                }

                // Step 4: Assemble groups with hard link deduplication
                for (sha256, files) in full_buckets {
                    if files.len() < 2 {
                        continue;
                    }

                    // Count unique inodes to prevent inflating reclaimable space on hardlinks
                    let mut unique_inodes: HashSet<(u64, u64)> = HashSet::new();
                    for file in &files {
                        if let Ok(identity) = FileIdentity::from_path(file) {
                            unique_inodes.insert((identity.device, identity.inode));
                        }
                    }

                    // If all files share the exact same inode, reclaimable is 0!
                    let distinct_copies = unique_inodes.len().max(1);
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
