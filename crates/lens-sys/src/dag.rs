use crate::model::SystemdUnit;
use std::collections::{BTreeMap, BTreeSet};

#[derive(Debug, Clone, Default)]
pub struct OrderingGraph {
    pub adj: BTreeMap<String, BTreeSet<String>>,
    pub all_nodes: BTreeSet<String>,
}

impl OrderingGraph {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn build(units: &BTreeMap<String, SystemdUnit>) -> Self {
        let mut graph = OrderingGraph::new();

        for (name, unit) in units {
            graph.all_nodes.insert(name.clone());
            graph.adj.entry(name.clone()).or_default();

            // A has Before=B => A must start before B => edge A -> B
            for b in &unit.before {
                graph.all_nodes.insert(b.clone());
                graph.adj.entry(name.clone()).or_default().insert(b.clone());
            }

            // A has After=B => B must start before A => edge B -> A
            for b in &unit.after {
                graph.all_nodes.insert(b.clone());
                graph.adj.entry(b.clone()).or_default().insert(name.clone());
            }
        }

        graph
    }

    /// Detect cycles in the ordering graph using Tarjan's Strongly Connected Components algorithm.
    pub fn find_cycles(&self) -> Vec<Vec<String>> {
        let mut index = 0usize;
        let mut stack = Vec::new();
        let mut indices: BTreeMap<String, usize> = BTreeMap::new();
        let mut lowlink: BTreeMap<String, usize> = BTreeMap::new();
        let mut on_stack: BTreeSet<String> = BTreeSet::new();
        let mut sccs: Vec<Vec<String>> = Vec::new();

        for node in &self.all_nodes {
            if !indices.contains_key(node) {
                strongconnect(
                    node,
                    &self.adj,
                    &mut index,
                    &mut stack,
                    &mut indices,
                    &mut lowlink,
                    &mut on_stack,
                    &mut sccs,
                );
            }
        }

        // Filter SCCs: only keep those with > 1 node, or a single node with a self-loop
        let mut cycles = Vec::new();
        for mut scc in sccs {
            if scc.len() > 1 {
                scc.sort();
                cycles.push(scc);
            } else if let Some(single) = scc.first() {
                if let Some(neighbors) = self.adj.get(single) {
                    if neighbors.contains(single) {
                        cycles.push(scc);
                    }
                }
            }
        }

        cycles.sort();
        cycles
    }

    /// Perform a topological sort on DAG (if acyclic).
    pub fn topological_sort(&self) -> Option<Vec<String>> {
        let mut in_degree: BTreeMap<String, usize> = BTreeMap::new();
        for node in &self.all_nodes {
            in_degree.insert(node.clone(), 0);
        }

        for targets in self.adj.values() {
            for t in targets {
                *in_degree.entry(t.clone()).or_default() += 1;
            }
        }

        let mut queue: Vec<String> = in_degree
            .iter()
            .filter(|(_, &deg)| deg == 0)
            .map(|(n, _)| n.clone())
            .collect();
        queue.sort();

        let mut order = Vec::new();

        while let Some(node) = queue.pop() {
            order.push(node.clone());
            if let Some(neighbors) = self.adj.get(&node) {
                for next in neighbors {
                    if let Some(deg) = in_degree.get_mut(next) {
                        *deg -= 1;
                        if *deg == 0 {
                            queue.push(next.clone());
                            queue.sort();
                        }
                    }
                }
            }
        }

        if order.len() == self.all_nodes.len() {
            Some(order)
        } else {
            None // Cycle exists
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn strongconnect(
    node: &str,
    adj: &BTreeMap<String, BTreeSet<String>>,
    index: &mut usize,
    stack: &mut Vec<String>,
    indices: &mut BTreeMap<String, usize>,
    lowlink: &mut BTreeMap<String, usize>,
    on_stack: &mut BTreeSet<String>,
    sccs: &mut Vec<Vec<String>>,
) {
    indices.insert(node.to_string(), *index);
    lowlink.insert(node.to_string(), *index);
    *index += 1;

    stack.push(node.to_string());
    on_stack.insert(node.to_string());

    if let Some(neighbors) = adj.get(node) {
        for next in neighbors {
            if !indices.contains_key(next) {
                strongconnect(next, adj, index, stack, indices, lowlink, on_stack, sccs);
                let next_low = lowlink[next];
                let node_low = lowlink.get_mut(node).unwrap();
                *node_low = (*node_low).min(next_low);
            } else if on_stack.contains(next) {
                let next_idx = indices[next];
                let node_low = lowlink.get_mut(node).unwrap();
                *node_low = (*node_low).min(next_idx);
            }
        }
    }

    if lowlink[node] == indices[node] {
        let mut scc = Vec::new();
        while let Some(w) = stack.pop() {
            on_stack.remove(&w);
            scc.push(w.clone());
            if w == node {
                break;
            }
        }
        sccs.push(scc);
    }
}
