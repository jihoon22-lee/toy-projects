pub mod diff;
pub mod model;
pub mod parser;

pub use diff::{diff_net_reports, NetDiffReport};
pub use model::{NetReport, NetSummary, SocketEntry, SocketKind, SocketProcess, TcpState};
pub use parser::inspect_network;
