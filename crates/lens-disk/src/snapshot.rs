use serde::{Deserialize, Serialize};
use std::collections::HashMap;

use crate::arena::{ArenaTree, DiskMetadata, FsKind, NodeId};

pub const SCHEMA_V2: &str = "diskmap.snapshot/v2";

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnapshotIdentityV2 {
    pub device: u64,
    pub file: u64,
    pub valid: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnapshotMetadataV2 {
    pub allocated_size: u64,
    pub group: u64,
    pub hard_link_count: u64,
    pub hard_link_count_known: bool,
    pub identity: SnapshotIdentityV2,
    pub kind: String,
    pub logical_size: u64,
    pub modified_ns: i64,
    pub modified_time_known: bool,
    pub owner: u64,
    pub ownership_known: bool,
    pub permissions: u32,
    pub permissions_known: bool,
}

impl Default for SnapshotMetadataV2 {
    fn default() -> Self {
        Self {
            allocated_size: 0,
            group: 0,
            hard_link_count: 1,
            hard_link_count_known: true,
            identity: SnapshotIdentityV2 {
                device: 0,
                file: 0,
                valid: false,
            },
            kind: "other".to_string(),
            logical_size: 0,
            modified_ns: 0,
            modified_time_known: false,
            owner: 0,
            ownership_known: false,
            permissions: 0,
            permissions_known: false,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnapshotNodeV2 {
    pub allocated_size: u64,
    pub allocated_size_known: bool,
    pub children: Vec<SnapshotNodeV2>,
    pub complete: bool,
    pub cycle_skipped: bool,
    pub error: String,
    pub followed: bool,
    pub has_target_metadata: bool,
    pub is_dir: bool,
    pub logical_size_known: bool,
    pub metadata: SnapshotMetadataV2,
    pub mount_boundary_skipped: bool,
    pub name: String,
    pub name_bytes: String,
    pub path: String,
    pub path_bytes: String,
    pub reclaimable_size: u64,
    pub reclaimable_size_known: bool,
    pub size: u64,
    pub target_metadata: SnapshotMetadataV2,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnapshotV2 {
    pub schema_version: String,
    pub complete: bool,
    pub truncated: bool,
    pub node_count: u64,
    pub root: SnapshotNodeV2,
}

impl SnapshotV2 {
    pub fn from_tree(tree: &ArenaTree, root_id: NodeId, complete: bool, truncated: bool) -> Self {
        let root = Self::build_snapshot_node(tree, root_id);
        Self {
            schema_version: SCHEMA_V2.to_string(),
            complete,
            truncated,
            node_count: tree.len() as u64,
            root,
        }
    }

    fn build_snapshot_node(tree: &ArenaTree, node_id: NodeId) -> SnapshotNodeV2 {
        let node = &tree.nodes[node_id as usize];
        let mut children = Vec::new();
        for child_id in tree.children_ids(node_id) {
            children.push(Self::build_snapshot_node(tree, child_id));
        }

        // Sort children stably by name
        children.sort_by(|a, b| a.name.cmp(&b.name));

        SnapshotNodeV2 {
            allocated_size: node.allocated_size,
            allocated_size_known: node.allocated_size_known,
            children,
            complete: node.complete,
            cycle_skipped: node.cycle_skipped,
            error: node.error.clone(),
            followed: node.followed,
            has_target_metadata: node.target_metadata.is_some(),
            is_dir: node.is_dir,
            logical_size_known: node.logical_size_known,
            metadata: Self::convert_metadata(&node.metadata),
            mount_boundary_skipped: node.mount_boundary_skipped,
            name: node.name.clone(),
            name_bytes: hex_encode(node.name.as_bytes()),
            path: node.rel_path.clone(),
            path_bytes: hex_encode(node.rel_path.as_bytes()),
            reclaimable_size: node.reclaimable_size,
            reclaimable_size_known: node.reclaimable_size_known,
            size: node.size,
            target_metadata: node
                .target_metadata
                .as_ref()
                .map(Self::convert_metadata)
                .unwrap_or_default(),
        }
    }

    fn convert_metadata(meta: &DiskMetadata) -> SnapshotMetadataV2 {
        let kind_str = match meta.kind {
            FsKind::RegularFile => "regular_file",
            FsKind::Directory => "directory",
            FsKind::Symlink => "symlink",
            FsKind::Other => "other",
        };

        SnapshotMetadataV2 {
            allocated_size: meta.allocated_size,
            group: meta.group,
            hard_link_count: meta.hard_link_count,
            hard_link_count_known: meta.hard_link_count_known,
            identity: SnapshotIdentityV2 {
                device: meta.identity.device,
                file: meta.identity.inode,
                valid: meta.identity.inode > 0,
            },
            kind: kind_str.to_string(),
            logical_size: meta.logical_size,
            modified_ns: meta.modified_ns,
            modified_time_known: meta.modified_time_known,
            owner: meta.owner,
            ownership_known: meta.ownership_known,
            permissions: meta.permissions,
            permissions_known: meta.permissions_known,
        }
    }
}

fn hex_encode(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        use std::fmt::Write;
        let _ = write!(&mut s, "{:02x}", b);
    }
    s
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnapshotDiff {
    pub baseline_root: String,
    pub candidate_root: String,
    pub added: Vec<String>,
    pub removed: Vec<String>,
    pub grown: Vec<String>,
    pub shrunk: Vec<String>,
    pub net_bytes_delta: i64,
}

impl SnapshotDiff {
    pub fn compute(left: &SnapshotV2, right: &SnapshotV2) -> Self {
        let mut left_map = HashMap::new();
        Self::collect_files(&left.root, &mut left_map);

        let mut right_map = HashMap::new();
        Self::collect_files(&right.root, &mut right_map);

        let mut added = Vec::new();
        let mut removed = Vec::new();
        let mut grown = Vec::new();
        let mut shrunk = Vec::new();

        for (path, &right_size) in &right_map {
            match left_map.get(path) {
                None => added.push(format!("+ {} ({} B)", path, right_size)),
                Some(&left_size) => {
                    if right_size > left_size {
                        grown.push(format!("^ {} (+{} B)", path, right_size - left_size));
                    } else if right_size < left_size {
                        shrunk.push(format!("v {} (-{} B)", path, left_size - right_size));
                    }
                }
            }
        }

        for (path, &left_size) in &left_map {
            if !right_map.contains_key(path) {
                removed.push(format!("- {} ({} B)", path, left_size));
            }
        }

        added.sort();
        removed.sort();
        grown.sort();
        shrunk.sort();

        let net_bytes_delta = right.root.size as i64 - left.root.size as i64;

        Self {
            baseline_root: left.root.name.clone(),
            candidate_root: right.root.name.clone(),
            added,
            removed,
            grown,
            shrunk,
            net_bytes_delta,
        }
    }

    fn collect_files(node: &SnapshotNodeV2, map: &mut HashMap<String, u64>) {
        if !node.is_dir {
            map.insert(node.path.clone(), node.size);
        }
        for child in &node.children {
            Self::collect_files(child, map);
        }
    }
}
