use clap::Parser;
use crossterm::{
    event::{self, Event, KeyCode},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{
    backend::CrosstermBackend,
    layout::{Constraint, Direction, Layout},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{Block, Borders, Gauge, List, ListItem, Paragraph, Tabs},
    Terminal,
};
use std::io;
use std::path::PathBuf;
use std::time::Duration;

use lens_tui::{TuiApp, TuiTab};

#[derive(Parser)]
#[command(name = "lens-tui")]
#[command(about = "Interactive Terminal UI Dashboard for Lens Platform")]
struct Cli {
    /// Initial path to inspect
    #[arg(default_value = ".")]
    path: PathBuf,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Cli::parse();
    let mut app = TuiApp::new(&args.path);

    enable_raw_mode()?;
    let mut stdout = io::stdout();
    execute!(stdout, EnterAlternateScreen)?;
    let backend = CrosstermBackend::new(stdout);
    let mut terminal = Terminal::new(backend)?;

    let res = run_app(&mut terminal, &mut app);

    disable_raw_mode()?;
    execute!(terminal.backend_mut(), LeaveAlternateScreen)?;
    terminal.show_cursor()?;

    if let Err(err) = res {
        eprintln!("TUI Error: {:?}", err);
    }

    Ok(())
}

fn run_app<B: ratatui::backend::Backend>(
    terminal: &mut Terminal<B>,
    app: &mut TuiApp,
) -> io::Result<()> {
    loop {
        terminal.draw(|f| {
            let chunks = Layout::default()
                .direction(Direction::Vertical)
                .constraints([
                    Constraint::Length(3), // Tabs
                    Constraint::Min(10),   // Content
                    Constraint::Length(3), // Status footer
                ])
                .split(f.area());

            let titles = TuiTab::all()
                .iter()
                .map(|t| Line::from(t.title()))
                .collect::<Vec<_>>();
            let tabs = Tabs::new(titles)
                .block(
                    Block::default()
                        .borders(Borders::ALL)
                        .title(" Lens Platform — Views [Tab / 1-4] "),
                )
                .select(match app.active_tab {
                    TuiTab::Storage => 0,
                    TuiTab::Services => 1,
                    TuiTab::Logs => 2,
                    TuiTab::Network => 3,
                })
                .style(Style::default().fg(Color::Cyan))
                .highlight_style(
                    Style::default()
                        .fg(Color::Yellow)
                        .add_modifier(Modifier::BOLD),
                );
            f.render_widget(tabs, chunks[0]);

            match app.active_tab {
                TuiTab::Storage => {
                    let body_chunks = Layout::default()
                        .direction(Direction::Horizontal)
                        .constraints([Constraint::Percentage(50), Constraint::Percentage(50)])
                        .split(chunks[1]);

                    let list_items: Vec<ListItem> = app
                        .items
                        .iter()
                        .enumerate()
                        .map(|(idx, item)| {
                            let prefix = if item.is_dir { "📁 " } else { "📄 " };
                            let style = if idx == app.selected_index {
                                Style::default()
                                    .fg(Color::Yellow)
                                    .add_modifier(Modifier::BOLD)
                            } else {
                                Style::default()
                            };
                            let content = format!(
                                "{}{}  ({:.2} MB)",
                                prefix,
                                item.name,
                                item.size as f64 / 1_048_576.0
                            );
                            ListItem::new(Line::from(content)).style(style)
                        })
                        .collect();

                    let list = List::new(list_items).block(
                        Block::default()
                            .borders(Borders::ALL)
                            .title(format!(" Directory: {:?} (j/k) ", app.current_path)),
                    );
                    f.render_widget(list, body_chunks[0]);

                    let right_chunks = Layout::default()
                        .direction(Direction::Vertical)
                        .constraints([Constraint::Length(8), Constraint::Min(4)])
                        .split(body_chunks[1]);

                    if let Some(selected) = app.items.get(app.selected_index) {
                        let pct = if app.total_size > 0 {
                            (selected.size as f64 / app.total_size as f64).min(1.0)
                        } else {
                            0.0
                        };

                        let detail_text = vec![
                            Line::from(vec![
                                Span::raw("Name: "),
                                Span::styled(
                                    &selected.name,
                                    Style::default().add_modifier(Modifier::BOLD),
                                ),
                            ]),
                            Line::from(format!("Type: {}", if selected.is_dir { "Directory" } else { "Regular File" })),
                            Line::from(format!(
                                "Logical Size: {} bytes ({:.2} MB)",
                                selected.size,
                                selected.size as f64 / 1_048_576.0
                            )),
                            Line::from(format!("Allocated Size: {} bytes", selected.allocated_size)),
                            Line::from(format!("Relative Share: {:.1}%", pct * 100.0)),
                        ];
                        let detail = Paragraph::new(detail_text).block(
                            Block::default()
                                .borders(Borders::ALL)
                                .title(" Storage Node Details "),
                        );
                        f.render_widget(detail, right_chunks[0]);

                        let gauge = Gauge::default()
                            .block(
                                Block::default()
                                    .borders(Borders::ALL)
                                    .title(" Footprint Share "),
                            )
                            .gauge_style(Style::default().fg(Color::Green))
                            .ratio(pct);
                        f.render_widget(gauge, right_chunks[1]);
                    } else {
                        let empty = Paragraph::new("No items in directory.").block(
                            Block::default()
                                .borders(Borders::ALL)
                                .title(" Storage Node Details "),
                        );
                        f.render_widget(empty, right_chunks[0]);
                    }
                }
                TuiTab::Network => {
                    let body_chunks = Layout::default()
                        .direction(Direction::Horizontal)
                        .constraints([Constraint::Percentage(55), Constraint::Percentage(45)])
                        .split(chunks[1]);

                    let listening = app.net_report.as_ref().map(|r| &r.listening);
                    let list_items: Vec<ListItem> = listening
                        .map(|entries| {
                            entries
                                .iter()
                                .map(|l| {
                                    let proc = l
                                        .process
                                        .as_ref()
                                        .map(|p| p.name.as_str())
                                        .unwrap_or("<orphan>");
                                    let line = format!(
                                        "{:<6} {:<20} (PID: {:?})",
                                        format!("{:?}", l.kind),
                                        format!("{}:{}", l.local_address, l.local_port),
                                        proc
                                    );
                                    ListItem::new(Line::from(line))
                                })
                                .collect()
                        })
                        .unwrap_or_default();

                    let net_list = List::new(list_items).block(
                        Block::default()
                            .borders(Borders::ALL)
                            .title(" Active Listening Ports & Sockets "),
                    );
                    f.render_widget(net_list, body_chunks[0]);

                    let summary_text = if let Some(rep) = &app.net_report {
                        vec![
                            Line::from(format!("Total Sockets:          {}", rep.summary.total_sockets)),
                            Line::from(format!("Listening Ports:        {}", rep.summary.listening_ports)),
                            Line::from(format!("Established Connections: {}", rep.summary.established_connections)),
                            Line::from(format!("TIME_WAIT Sockets:      {}", rep.summary.time_wait_sockets)),
                            Line::from(format!("Orphan Sockets:         {}", rep.summary.orphan_sockets)),
                            Line::from(format!("UNIX Domain Sockets:    {}", rep.summary.unix_domain_sockets)),
                        ]
                    } else {
                        vec![Line::from("Network inspection unavailable (/proc not mounted)")]
                    };

                    let detail = Paragraph::new(summary_text).block(
                        Block::default()
                            .borders(Borders::ALL)
                            .title(" Network Summary "),
                    );
                    f.render_widget(detail, body_chunks[1]);
                }
                TuiTab::Services => {
                    let info = Paragraph::new(vec![
                        Line::from("Systemd Services & Dependency DAG Inspection"),
                        Line::from(""),
                        Line::from("Run 'lens sys inspect /etc/systemd/system' for detailed DAG cycle detection."),
                        Line::from("Keybinding: [Tab] to cycle views, [1] for Storage, [4] for Network."),
                    ])
                    .block(Block::default().borders(Borders::ALL).title(" Services Inspection "));
                    f.render_widget(info, chunks[1]);
                }
                TuiTab::Logs => {
                    let info = Paragraph::new(vec![
                        Line::from("High-Performance Memory-Mapped Log Search & Indexer"),
                        Line::from(""),
                        Line::from("Run 'lens log filter <PATH> --query <KEYWORD>' for instant sub-millisecond filtering."),
                        Line::from("Keybinding: [Tab] to cycle views, [1] for Storage, [4] for Network."),
                    ])
                    .block(Block::default().borders(Borders::ALL).title(" Log Inspection "));
                    f.render_widget(info, chunks[1]);
                }
            }

            // Footer
            let footer = Paragraph::new(format!(
                " {} | Hotkeys: [Tab/1-4] Views  [q] Quit  [j/k] Navigate  [Enter] Open  [Backspace/h] Parent",
                app.status_message
            ))
            .style(Style::default().fg(Color::DarkGray))
            .block(Block::default().borders(Borders::ALL));
            f.render_widget(footer, chunks[2]);
        })?;

        if event::poll(Duration::from_millis(100))? {
            if let Event::Key(key) = event::read()? {
                match key.code {
                    KeyCode::Char('q') | KeyCode::Esc => return Ok(()),
                    KeyCode::Tab => app.next_tab(),
                    KeyCode::Char('1') => app.set_tab(TuiTab::Storage),
                    KeyCode::Char('2') => app.set_tab(TuiTab::Services),
                    KeyCode::Char('3') => app.set_tab(TuiTab::Logs),
                    KeyCode::Char('4') => app.set_tab(TuiTab::Network),
                    KeyCode::Char('j') | KeyCode::Down => app.next(),
                    KeyCode::Char('k') | KeyCode::Up => app.previous(),
                    KeyCode::Enter | KeyCode::Char('l') => app.enter(),
                    KeyCode::Backspace | KeyCode::Char('h') => app.parent(),
                    KeyCode::Char('r') => {
                        app.reload();
                        app.reload_network();
                    }
                    _ => {}
                }
            }
        }
    }
}
