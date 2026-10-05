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
    widgets::{Block, Borders, Gauge, List, ListItem, Paragraph},
    Terminal,
};
use std::io;
use std::path::PathBuf;
use std::time::Duration;

use lens_tui::TuiApp;

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
                    Constraint::Length(3), // Header
                    Constraint::Min(10),   // Content (Left: List, Right: Details)
                    Constraint::Length(3), // Status footer
                ])
                .split(f.area());

            // Header
            let header = Paragraph::new(format!(
                " Lens Platform Dashboard — Path: {:?} (Total: {:.2} MB, {} items)",
                app.current_path,
                app.total_size as f64 / 1_048_576.0,
                app.items.len()
            ))
            .style(
                Style::default()
                    .fg(Color::Cyan)
                    .add_modifier(Modifier::BOLD),
            )
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .title(" Systems Diagnostics "),
            );
            f.render_widget(header, chunks[0]);

            // Middle: 2 columns
            let body_chunks = Layout::default()
                .direction(Direction::Horizontal)
                .constraints([Constraint::Percentage(50), Constraint::Percentage(50)])
                .split(chunks[1]);

            // Items list
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
                    .title(" Directory Entries (VIM: j/k) "),
            );
            f.render_widget(list, body_chunks[0]);

            // Right: Detail panel
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
                    Line::from(format!(
                        "Type: {}",
                        if selected.is_dir { "Directory" } else { "File" }
                    )),
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
                        .title(" Item Details "),
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
                        .title(" Item Details "),
                );
                f.render_widget(empty, right_chunks[0]);
            }

            // Footer
            let footer = Paragraph::new(format!(
                " {} | Hotkeys: [q] Quit  [j/k] Down/Up  [Enter] Open  [Backspace/h] Parent",
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
                    KeyCode::Char('j') | KeyCode::Down => app.next(),
                    KeyCode::Char('k') | KeyCode::Up => app.previous(),
                    KeyCode::Enter | KeyCode::Char('l') => app.enter(),
                    KeyCode::Backspace | KeyCode::Char('h') => app.parent(),
                    KeyCode::Char('r') => app.reload(),
                    _ => {}
                }
            }
        }
    }
}
