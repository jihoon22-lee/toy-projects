use quick_xml::events::Event;
use quick_xml::reader::Reader;

use crate::model::*;
use lens_core::{LensError, Result};

pub fn parse_junit_xml(xml_bytes: &[u8], project_name: &str) -> Result<TestRun> {
    let mut reader = Reader::from_reader(xml_bytes);
    reader.config_mut().trim_text(true);

    let mut buf = Vec::new();
    let mut cases = Vec::new();
    let mut summary = TestSummary::default();

    let mut current_name = String::new();
    let mut current_classname = String::new();
    let mut current_time = 0.0f64;
    let mut current_status = TestStatus::Passed;
    let mut current_message = None;
    let mut current_output = String::new();
    let mut current_body = String::new();
    // Which element's character data we are accumulating.
    let mut capture: Option<&'static str> = None;
    let mut in_testcase = false;
    // Depth of unclosed elements; a truncated document leaves this > 0 at EOF.
    let mut open_depth = 0usize;

    // Decode `&quot;`/`&amp;`-style entities: attr.value is the raw bytes.
    let decoder = reader.decoder();
    let decode_attr = |attr: &quick_xml::events::attributes::Attribute| -> String {
        attr.decode_and_unescape_value(decoder)
            .map(|c| c.into_owned())
            .unwrap_or_else(|_| String::from_utf8_lossy(&attr.value).into_owned())
    };

    loop {
        match reader.read_event_into(&mut buf) {
            Ok(Event::Start(ref e)) => {
                open_depth += 1;
                match e.name().as_ref() {
                    b"testcase" => {
                        in_testcase = true;
                        current_status = TestStatus::Passed;
                        current_message = None;
                        current_output.clear();
                        current_body.clear();
                        capture = None;
                        current_name.clear();
                        current_classname.clear();
                        current_time = 0.0;

                        for attr in e.attributes().flatten() {
                            match attr.key.as_ref() {
                                b"name" => current_name = decode_attr(&attr),
                                b"classname" => current_classname = decode_attr(&attr),
                                b"time" => {
                                    if let Ok(s) = std::str::from_utf8(&attr.value) {
                                        current_time = s.parse::<f64>().unwrap_or(0.0);
                                    }
                                }
                                _ => {}
                            }
                        }
                    }
                    b"failure" => {
                        if in_testcase {
                            current_status = TestStatus::Failed;
                            capture = Some("failure");
                            current_body.clear();
                            for attr in e.attributes().flatten() {
                                if attr.key.as_ref() == b"message" {
                                    current_message = Some(decode_attr(&attr));
                                }
                            }
                        }
                    }
                    b"error" => {
                        if in_testcase {
                            current_status = TestStatus::Error;
                            capture = Some("error");
                            current_body.clear();
                            for attr in e.attributes().flatten() {
                                if attr.key.as_ref() == b"message" {
                                    current_message = Some(decode_attr(&attr));
                                }
                            }
                        }
                    }
                    b"skipped" if in_testcase => {
                        current_status = TestStatus::Skipped;
                        capture = Some("skipped");
                        current_body.clear();
                    }
                    b"system-out" | b"system-err" if in_testcase => {
                        capture = Some("system");
                        current_body.clear();
                    }
                    _ => {}
                }
            }
            Ok(Event::Empty(ref e)) => {
                if e.name().as_ref() == b"testcase" {
                    let mut name = String::new();
                    let mut classname = String::new();
                    let mut time = 0.0;

                    for attr in e.attributes().flatten() {
                        match attr.key.as_ref() {
                            b"name" => name = decode_attr(&attr),
                            b"classname" => classname = decode_attr(&attr),
                            b"time" => {
                                if let Ok(s) = std::str::from_utf8(&attr.value) {
                                    time = s.parse::<f64>().unwrap_or(0.0);
                                }
                            }
                            _ => {}
                        }
                    }

                    let identity = if classname.is_empty() {
                        name.clone()
                    } else {
                        format!("{}::{}", classname, name)
                    };

                    summary.total += 1;
                    summary.passed += 1;
                    summary.duration_sec += time;

                    cases.push(TestCase {
                        identity,
                        name,
                        classname,
                        status: TestStatus::Passed,
                        duration_sec: time,
                        message: None,
                        output: None,
                    });
                } else if in_testcase {
                    match e.name().as_ref() {
                        b"failure" => {
                            current_status = TestStatus::Failed;
                            for attr in e.attributes().flatten() {
                                if attr.key.as_ref() == b"message" {
                                    current_message = Some(decode_attr(&attr));
                                }
                            }
                        }
                        b"error" => {
                            current_status = TestStatus::Error;
                            for attr in e.attributes().flatten() {
                                if attr.key.as_ref() == b"message" {
                                    current_message = Some(decode_attr(&attr));
                                }
                            }
                        }
                        b"skipped" => {
                            current_status = TestStatus::Skipped;
                        }
                        _ => {}
                    }
                }
            }
            Ok(Event::Text(ref e)) => {
                if capture.is_some() && in_testcase {
                    let text = quick_xml::escape::unescape(&String::from_utf8_lossy(e))
                        .map(|c| c.into_owned())
                        .unwrap_or_else(|_| String::from_utf8_lossy(e).into_owned());
                    current_body.push_str(&text);
                    current_body.push('\n');
                }
            }
            Ok(Event::CData(ref e)) => {
                if capture.is_some() && in_testcase {
                    current_body.push_str(&String::from_utf8_lossy(e));
                    current_body.push('\n');
                }
            }
            Ok(Event::End(ref e)) => {
                open_depth = open_depth.saturating_sub(1);
                // Flush captured character data when a tracked element closes.
                let name = e.name();
                let name = name.as_ref();
                if let Some(kind) = capture.take_if(|k| {
                    matches!(
                        (*k, name),
                        ("failure", b"failure")
                            | ("error", b"error")
                            | ("skipped", b"skipped")
                            | ("system", b"system-out")
                            | ("system", b"system-err")
                    )
                }) {
                    let body = current_body.trim().to_string();
                    match kind {
                        "system" => {
                            if !body.is_empty() {
                                if !current_output.is_empty() {
                                    current_output.push('\n');
                                }
                                current_output.push_str(&body);
                            }
                        }
                        _ => {
                            // Use the element body as the message when the
                            // `message` attribute was absent.
                            if current_message.is_none() && !body.is_empty() {
                                current_message = Some(body);
                            }
                        }
                    }
                }
                if name == b"testcase" && in_testcase {
                    in_testcase = false;

                    let identity = if current_classname.is_empty() {
                        current_name.clone()
                    } else {
                        format!("{}::{}", current_classname, current_name)
                    };

                    summary.total += 1;
                    match current_status {
                        TestStatus::Passed => summary.passed += 1,
                        TestStatus::Failed => summary.failed += 1,
                        TestStatus::Error => summary.errors += 1,
                        TestStatus::Skipped => summary.skipped += 1,
                    }
                    summary.duration_sec += current_time;

                    cases.push(TestCase {
                        identity,
                        name: current_name.clone(),
                        classname: current_classname.clone(),
                        status: current_status,
                        duration_sec: current_time,
                        message: current_message.clone(),
                        output: if current_output.is_empty() {
                            None
                        } else {
                            Some(current_output.clone())
                        },
                    });
                }
            }
            Ok(Event::Eof) => break,
            Err(e) => {
                return Err(LensError::InvalidInput {
                    message: format!("XML parsing error: {}", e),
                });
            }
            _ => {}
        }
        buf.clear();
    }

    // Deterministic content-addressed run id: identical inputs produce
    // identical ids so reports diff cleanly across machines and time.
    let run_id = {
        let mut h = lens_core::IncrementalHasher::new();
        h.update(project_name.as_bytes());
        for c in &cases {
            h.update(c.identity.as_bytes());
            h.update(&[c.status as u8]);
        }
        let digest = h.finish();
        format!("{}-{}", project_name, &digest[..12])
    };

    Ok(TestRun {
        schema: RUN_SCHEMA_V1.to_string(),
        producer: TestProducer {
            name: "testlens".to_string(),
            version: env!("CARGO_PKG_VERSION").to_string(),
        },
        run_id,
        project: project_name.to_string(),
        collected_at: lens_core::time::utc_now_iso(),
        complete: open_depth == 0,
        summary,
        cases,
    })
}
