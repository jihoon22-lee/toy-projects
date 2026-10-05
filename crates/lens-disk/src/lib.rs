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

    #[test]
    fn test_trash_moves_symlink_not_target() {
        let dir = tempdir().unwrap();
        let target_file = dir.path().join("evidence.txt");
        fs::write(&target_file, b"keep me").unwrap();
        let link = dir.path().join("link");
        std::os::unix::fs::symlink(&target_file, &link).unwrap();

        let trash = TrashManager::new(dir.path().join("trash"));
        let receipt = trash.move_to_trash(&link).unwrap();

        // The link was trashed; the target must be untouched.
        assert!(target_file.exists());
        assert!(link.symlink_metadata().is_err());
        assert!(receipt
            .trashed_file_path
            .symlink_metadata()
            .unwrap()
            .file_type()
            .is_symlink());
    }

    #[test]
    fn test_symlink_alias_does_not_hide_real_dir() {
        // A symlink sorting earlier (`0link`) must not mark the real dir's
        // inode visited and hide its subtree from the scan.
        let dir = tempdir().unwrap();
        let real = dir.path().join("zreal");
        fs::create_dir(&real).unwrap();
        fs::write(real.join("secret.txt"), b"evidence").unwrap();
        std::os::unix::fs::symlink(&real, dir.path().join("0link")).unwrap();

        let scanner = DiskScanner::new(ScanOptions::default());
        let res = scanner.scan(dir.path()).unwrap();

        assert_eq!(res.scanned_entries, 4); // root, link, real dir, secret.txt
        let names: Vec<String> = res.tree.nodes.iter().map(|n| n.name.clone()).collect();
        assert!(names.contains(&"secret.txt".to_string()));
        assert_eq!(names.iter().filter(|n| *n == "zreal").count(), 1);
    }

    #[test]
    fn test_parallel_scan_matches_serial() {
        let dir = tempdir().unwrap();
        for i in 0..8 {
            let sub = dir.path().join(format!("d{}", i));
            fs::create_dir(&sub).unwrap();
            for j in 0..4 {
                fs::write(sub.join(format!("f{}.bin", j)), vec![i as u8; 64]).unwrap();
            }
        }

        let serial = DiskScanner::new(ScanOptions::default())
            .scan(dir.path())
            .unwrap();
        let parallel = DiskScanner::new(ScanOptions::default())
            .with_parallel(true)
            .scan(dir.path())
            .unwrap();

        assert_eq!(serial.scanned_entries, parallel.scanned_entries);
        assert_eq!(serial.complete, parallel.complete);
        assert_eq!(serial.errors, parallel.errors);
        let serial_names: Vec<String> = serial.tree.nodes.iter().map(|n| n.name.clone()).collect();
        let parallel_names: Vec<String> =
            parallel.tree.nodes.iter().map(|n| n.name.clone()).collect();
        assert_eq!(serial_names, parallel_names);
    }

    #[test]
    fn test_depth_cap_marks_scan_incomplete() {
        let dir = tempdir().unwrap();
        let mut deep = dir.path().join("a");
        fs::create_dir(&deep).unwrap();
        for c in ["b", "c"] {
            deep = deep.join(c);
            fs::create_dir(&deep).unwrap();
        }
        fs::write(deep.join("hidden.txt"), b"x").unwrap();

        let opts = ScanOptions {
            max_depth: 2,
            ..Default::default()
        };
        let res = DiskScanner::new(opts).scan(dir.path()).unwrap();

        assert!(!res.complete);
        assert!(res.errors.iter().any(|e| e.contains("Depth limit")));
    }
}
