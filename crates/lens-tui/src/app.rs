use lens_disk::{DiskScanner, ScanOptions, ScanResult};
use std::path::{Path, PathBuf};

#[derive(Debug)]
pub struct TuiItem {
    pub name: String,
    pub path: PathBuf,
    pub is_dir: bool,
    pub size: u64,
    pub allocated_size: u64,
    pub children_count: usize,
}

pub struct TuiApp {
    pub current_path: PathBuf,
    pub items: Vec<TuiItem>,
    pub selected_index: usize,
    pub total_size: u64,
    pub status_message: String,
    pub should_quit: bool,
}

impl TuiApp {
    pub fn new(initial_path: &Path) -> Self {
        let mut app = Self {
            current_path: initial_path.to_path_buf(),
            items: Vec::new(),
            selected_index: 0,
            total_size: 0,
            status_message: "Ready. Use j/k to navigate, Enter to open, q to quit.".to_string(),
            should_quit: false,
        };
        app.reload();
        app
    }

    pub fn reload(&mut self) {
        let scanner = DiskScanner::new(ScanOptions::default());
        if let Ok(res) = scanner.scan(&self.current_path) {
            self.total_size = if res.tree.nodes.is_empty() {
                0
            } else {
                res.tree.nodes[res.root_id as usize].size
            };
            self.populate_items(&res);
        } else {
            self.status_message = format!("Failed to scan {:?}", self.current_path);
        }
    }

    fn populate_items(&mut self, res: &ScanResult) {
        self.items.clear();
        if res.tree.nodes.is_empty() {
            return;
        }

        let children = res.tree.children_ids(res.root_id);
        for child_id in children {
            let node = &res.tree.nodes[child_id as usize];
            let sub_children_count = res.tree.children_ids(child_id).len();
            self.items.push(TuiItem {
                name: node.name.clone(),
                path: self.current_path.join(&node.name),
                is_dir: node.is_dir,
                size: node.size,
                allocated_size: node.allocated_size,
                children_count: sub_children_count,
            });
        }

        self.items.sort_by_key(|b| std::cmp::Reverse(b.size));
        if self.selected_index >= self.items.len() {
            self.selected_index = 0;
        }
    }

    pub fn next(&mut self) {
        if !self.items.is_empty() {
            self.selected_index = (self.selected_index + 1) % self.items.len();
        }
    }

    pub fn previous(&mut self) {
        if !self.items.is_empty() {
            if self.selected_index == 0 {
                self.selected_index = self.items.len() - 1;
            } else {
                self.selected_index -= 1;
            }
        }
    }

    pub fn enter(&mut self) {
        if let Some(item) = self.items.get(self.selected_index) {
            if item.is_dir {
                self.current_path = item.path.clone();
                self.selected_index = 0;
                self.reload();
            }
        }
    }

    pub fn parent(&mut self) {
        if let Some(p) = self.current_path.parent() {
            self.current_path = p.to_path_buf();
            self.selected_index = 0;
            self.reload();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn test_tui_app_navigation() {
        let dir = tempdir().expect("tempdir");
        std::fs::create_dir(dir.path().join("sub")).expect("mkdir");
        std::fs::write(dir.path().join("file.txt"), "hello").expect("write");

        let mut app = TuiApp::new(dir.path());
        assert_eq!(app.items.len(), 2);

        app.next();
        assert_eq!(app.selected_index, 1);
        app.previous();
        assert_eq!(app.selected_index, 0);
    }
}
