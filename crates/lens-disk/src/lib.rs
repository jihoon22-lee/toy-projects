//! # Lens Disk (`lens-disk`)
//!
//! High-performance filesystem diagnostics, storage tree analysis, and safe cleanup workbench.
//!
//! Re-architects and replaces legacy `diskmap` with:
//! - **Arena Tree ([`arena::ArenaTree`])**: Contiguous `u32`-indexed node storage
//!   with O(1) child insertion and iterative aggregation.
//! - **Scanner ([`scanner::DiskScanner`])**: Bounded traversal with mount and cycle
//!   safeguards; optional rayon-parallel stat.
//! - **Duplicate Finder ([`duplicates::DuplicateFinder`])**: Multi-stage (size -> inode -> 4KB partial -> full SHA-256) with hardlink deduplication.
//! - **FreeDesktop Trash Manager ([`trash::TrashManager`])**: XDG spec-compliant trash movement and audit receipt restoration.
//! - **Snapshot V2 ([`snapshot::SnapshotV2`])**: 100% compliant with `diskmap.snapshot/v2` schema.

pub mod arena;
pub mod duplicates;
pub mod scanner;
pub mod snapshot;
pub mod trash;

pub use arena::{ArenaTree, DiskMetadata, FsKind, FsNode, NodeId};
pub use duplicates::{DuplicateFinder, DuplicateGroup};
pub use scanner::{DiskScanner, ScanOptions, ScanResult};
pub use snapshot::{SnapshotDiff, SnapshotNodeV2, SnapshotV2, SCHEMA_V2};
pub use trash::{TrashManager, TrashReceipt};

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn test_scan_and_snapshot_roundtrip() {
        let dir = tempdir().unwrap();
        let sub = dir.path().join("subdir");
        fs::create_dir(&sub).unwrap();
        fs::write(sub.join("file_a.txt"), b"12345678").unwrap();
        fs::write(sub.join("file_b.txt"), b"abcdef").unwrap();

        let scanner = DiskScanner::new(ScanOptions::default());
        let res = scanner.scan(dir.path()).unwrap();

        assert_eq!(res.scanned_entries, 4); // root, subdir, 2 files
        assert!(res.complete);

        let snap = SnapshotV2::from_tree(&res.tree, res.root_id, res.complete, res.truncated);
        assert_eq!(snap.schema_version, "diskmap.snapshot/v2");
        assert_eq!(snap.root.size, 14); // 8 + 6

        let json = serde_json::to_string(&snap).unwrap();
        assert!(json.contains("diskmap.snapshot/v2"));
    }

    #[test]
    fn test_duplicate_detection() {
        let dir = tempdir().unwrap();
        let f1 = dir.path().join("copy1.bin");
        let f2 = dir.path().join("copy2.bin");
        let f3 = dir.path().join("diff.bin");

        let content = vec![0x42u8; 8192];
        fs::write(&f1, &content).unwrap();
        fs::write(&f2, &content).unwrap();
        fs::write(&f3, b"short content").unwrap();

        let scanner = DiskScanner::new(ScanOptions::default());
        let res = scanner.scan(dir.path()).unwrap();

        let finder = DuplicateFinder::new(1);
        let groups = finder.find_in_tree(&res.tree, dir.path()).unwrap();

        assert_eq!(groups.len(), 1);
        assert_eq!(groups[0].size, 8192);
        assert_eq!(groups[0].files.len(), 2);
        assert_eq!(groups[0].reclaimable_bytes, 8192);
    }

    #[test]
    fn test_trash_and_restore() {
        let dir = tempdir().unwrap();
        let target_file = dir.path().join("doomed.txt");
        fs::write(&target_file, b"important data").unwrap();

        let trash_dir = dir.path().join("custom_trash");
        let trash = TrashManager::new(trash_dir);

        let receipt = trash.move_to_trash(&target_file).unwrap();
        assert!(!target_file.exists());
        assert!(receipt.trashed_file_path.exists());
        assert!(receipt.info_path.exists());

        // Restore
        trash.restore(&receipt).unwrap();
        assert!(target_file.exists());
        assert_eq!(fs::read(&target_file).unwrap(), b"important data");
    }
}
