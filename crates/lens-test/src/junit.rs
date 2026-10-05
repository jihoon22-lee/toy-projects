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
    let mut in_testcase = false;

    loop {
        match reader.read_event_into(&mut buf) {
            Ok(Event::Start(ref e)) => match e.name().as_ref() {
                b"testcase" => {
                    in_testcase = true;
                    current_status = TestStatus::Passed;
                    current_message = None;
                    current_name.clear();
                    current_classname.clear();
                    current_time = 0.0;

                    for attr in e.attributes().flatten() {
                        match attr.key.as_ref() {
                            b"name" => {
                                current_name = String::from_utf8_lossy(&attr.value).to_string();
                            }
                            b"classname" => {
                                current_classname =
                                    String::from_utf8_lossy(&attr.value).to_string();
                            }
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
                        for attr in e.attributes().flatten() {
                            if attr.key.as_ref() == b"message" {
                                current_message =
                                    Some(String::from_utf8_lossy(&attr.value).to_string());
                            }
                        }
                    }
                }
                b"error" => {
                    if in_testcase {
                        current_status = TestStatus::Error;
                        for attr in e.attributes().flatten() {
                            if attr.key.as_ref() == b"message" {
                                current_message =
                                    Some(String::from_utf8_lossy(&attr.value).to_string());
                            }
                        }
                    }
                }
                b"skipped" if in_testcase => {
                    current_status = TestStatus::Skipped;
                }
                _ => {}
            },
            Ok(Event::Empty(ref e)) => {
                if e.name().as_ref() == b"testcase" {
                    let mut name = String::new();
                    let mut classname = String::new();
                    let mut time = 0.0;

                    for attr in e.attributes().flatten() {
                        match attr.key.as_ref() {
                            b"name" => name = String::from_utf8_lossy(&attr.value).to_string(),
                            b"classname" => {
                                classname = String::from_utf8_lossy(&attr.value).to_string()
                            }
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
                    });
                } else if in_testcase {
                    match e.name().as_ref() {
                        b"failure" => {
                            current_status = TestStatus::Failed;
                            for attr in e.attributes().flatten() {
                                if attr.key.as_ref() == b"message" {
                                    current_message =
                                        Some(String::from_utf8_lossy(&attr.value).to_string());
                                }
                            }
                        }
                        b"error" => {
                            current_status = TestStatus::Error;
                            for attr in e.attributes().flatten() {
                                if attr.key.as_ref() == b"message" {
                                    current_message =
                                        Some(String::from_utf8_lossy(&attr.value).to_string());
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
            Ok(Event::End(ref e)) => {
                if e.name().as_ref() == b"testcase" && in_testcase {
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

    Ok(TestRun {
        schema: RUN_SCHEMA_V1.to_string(),
        producer: TestProducer {
            name: "testlens".to_string(),
            version: "0.3.0".to_string(),
        },
        run_id: format!(
            "{}-{}",
            project_name,
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs()
        ),
        project: project_name.to_string(),
        collected_at: "2026-10-05T16:50:00Z".to_string(),
        complete: true,
        summary,
        cases,
    })
}
