use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SocketKind {
    Tcp,
    Tcp6,
    Udp,
    Udp6,
    UnixStream,
    UnixDgram,
    Other,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum TcpState {
    Established,
    SynSent,
    SynRecv,
    FinWait1,
    FinWait2,
    TimeWait,
    Close,
    CloseWait,
    LastAck,
    Listen,
    Closing,
    Unknown,
}

impl TcpState {
    pub fn from_hex(hex: &str) -> Self {
        match hex.to_ascii_uppercase().as_str() {
            "01" | "1" => Self::Established,
            "02" | "2" => Self::SynSent,
            "03" | "3" => Self::SynRecv,
            "04" | "4" => Self::FinWait1,
            "05" | "5" => Self::FinWait2,
            "06" | "6" => Self::TimeWait,
            "07" | "7" => Self::Close,
            "08" | "8" => Self::CloseWait,
            "09" | "9" => Self::LastAck,
            "0A" | "A" => Self::Listen,
            "0B" | "B" => Self::Closing,
            _ => Self::Unknown,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SocketProcess {
    pub pid: u32,
    pub name: String,
    pub cmdline: String,
    pub fd: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SocketEntry {
    pub kind: SocketKind,
    pub local_address: String,
    pub local_port: u16,
    pub remote_address: String,
    pub remote_port: u16,
    pub state: TcpState,
    pub inode: u64,
    pub uid: u32,
    pub tx_queue: u64,
    pub rx_queue: u64,
    pub process: Option<SocketProcess>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub unix_path: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct NetSummary {
    pub total_sockets: usize,
    pub listening_ports: usize,
    pub established_connections: usize,
    pub time_wait_sockets: usize,
    pub orphan_sockets: usize,
    pub unix_domain_sockets: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct NetReport {
    pub schema_version: String,
    pub summary: NetSummary,
    pub sockets: Vec<SocketEntry>,
    pub listening: Vec<SocketEntry>,
}
