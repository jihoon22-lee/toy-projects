use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;

/// The conservative 3-state compatibility evaluation model.
///
/// Invariant: Unknown or ambiguous evidence MUST NOT be assumed compatible.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Compatibility {
    Compatible,
    Incompatible,
    Uncertain,
}

impl Compatibility {
    pub fn is_compatible(&self) -> bool {
        matches!(self, Self::Compatible)
    }

    pub fn is_uncertain(&self) -> bool {
        matches!(self, Self::Uncertain)
    }

    pub fn is_incompatible(&self) -> bool {
        matches!(self, Self::Incompatible)
    }

    pub fn combine(self, other: Self) -> Self {
        match (self, other) {
            (Self::Incompatible, _) | (_, Self::Incompatible) => Self::Incompatible,
            (Self::Uncertain, _) | (_, Self::Uncertain) => Self::Uncertain,
            (Self::Compatible, Self::Compatible) => Self::Compatible,
        }
    }
}

/// Generic ordered set difference between a baseline (left) and candidate (right).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct SetDiff<T: Ord> {
    pub added: Vec<T>,
    pub removed: Vec<T>,
}

impl<T: Ord + Clone> SetDiff<T> {
    pub fn compute<I1, I2>(left: I1, right: I2) -> Self
    where
        I1: IntoIterator<Item = T>,
        I2: IntoIterator<Item = T>,
    {
        let left_set: BTreeSet<T> = left.into_iter().collect();
        let right_set: BTreeSet<T> = right.into_iter().collect();

        let added: Vec<T> = right_set.difference(&left_set).cloned().collect();
        let removed: Vec<T> = left_set.difference(&right_set).cloned().collect();

        Self { added, removed }
    }

    pub fn has_changed(&self) -> bool {
        !self.added.is_empty() || !self.removed.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_set_diff() {
        let left = vec!["alpha", "beta", "gamma"];
        let right = vec!["beta", "gamma", "delta"];
        let diff = SetDiff::compute(left, right);

        assert_eq!(diff.added, vec!["delta"]);
        assert_eq!(diff.removed, vec!["alpha"]);
        assert!(diff.has_changed());
    }

    #[test]
    fn test_compatibility_combination() {
        assert_eq!(
            Compatibility::Compatible.combine(Compatibility::Compatible),
            Compatibility::Compatible
        );
        assert_eq!(
            Compatibility::Compatible.combine(Compatibility::Uncertain),
            Compatibility::Uncertain
        );
        assert_eq!(
            Compatibility::Uncertain.combine(Compatibility::Incompatible),
            Compatibility::Incompatible
        );
    }
}
