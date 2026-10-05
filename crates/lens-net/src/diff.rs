use serde::{Deserialize, Serialize};

use crate::model::*;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct NetDiffReport {
    pub new_listeners: Vec<SocketEntry>,
    pub closed_listeners: Vec<SocketEntry>,
    pub new_connections: Vec<SocketEntry>,
    pub closed_connections: Vec<SocketEntry>,
    pub time_wait_delta: i64,
}

pub fn diff_net_reports(left: &NetReport, right: &NetReport) -> NetDiffReport {
    let mut new_listeners = Vec::new();
    let mut closed_listeners = Vec::new();

    // Track listeners by (kind, port)
    for r in &right.listening {
        if !left
            .listening
            .iter()
            .any(|l| l.kind == r.kind && l.local_port == r.local_port)
        {
            new_listeners.push(r.clone());
        }
    }

    for l in &left.listening {
        if !right
            .listening
            .iter()
            .any(|r| r.kind == l.kind && r.local_port == l.local_port)
        {
            closed_listeners.push(l.clone());
        }
    }

    let mut new_connections = Vec::new();
    let mut closed_connections = Vec::new();

    for r in &right.sockets {
        if r.state == TcpState::Established
            && !left
                .sockets
                .iter()
                .any(|l| l.inode == r.inode && l.state == TcpState::Established)
        {
            new_connections.push(r.clone());
        }
    }

    for l in &left.sockets {
        if l.state == TcpState::Established
            && !right
                .sockets
                .iter()
                .any(|r| r.inode == l.inode && r.state == TcpState::Established)
        {
            closed_connections.push(l.clone());
        }
    }

    let time_wait_delta =
        right.summary.time_wait_sockets as i64 - left.summary.time_wait_sockets as i64;

    NetDiffReport {
        new_listeners,
        closed_listeners,
        new_connections,
        closed_connections,
        time_wait_delta,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_diff_net_reports() {
        let left = NetReport {
            schema_version: "lens.net/v1".to_string(),
            summary: NetSummary {
                total_sockets: 1,
                listening_ports: 1,
                time_wait_sockets: 2,
                ..Default::default()
            },
            sockets: vec![],
            listening: vec![SocketEntry {
                kind: SocketKind::Tcp,
                local_address: "127.0.0.1".to_string(),
                local_port: 80,
                remote_address: String::new(),
                remote_port: 0,
                state: TcpState::Listen,
                inode: 1,
                uid: 0,
                tx_queue: 0,
                rx_queue: 0,
                process: None,
                unix_path: None,
            }],
        };

        let right = NetReport {
            schema_version: "lens.net/v1".to_string(),
            summary: NetSummary {
                total_sockets: 1,
                listening_ports: 1,
                time_wait_sockets: 5,
                ..Default::default()
            },
            sockets: vec![],
            listening: vec![SocketEntry {
                kind: SocketKind::Tcp,
                local_address: "127.0.0.1".to_string(),
                local_port: 443,
                remote_address: String::new(),
                remote_port: 0,
                state: TcpState::Listen,
                inode: 2,
                uid: 0,
                tx_queue: 0,
                rx_queue: 0,
                process: None,
                unix_path: None,
            }],
        };

        let diff = diff_net_reports(&left, &right);
        assert_eq!(diff.new_listeners.len(), 1);
        assert_eq!(diff.new_listeners[0].local_port, 443);
        assert_eq!(diff.closed_listeners.len(), 1);
        assert_eq!(diff.closed_listeners[0].local_port, 80);
        assert_eq!(diff.time_wait_delta, 3);
    }
}
