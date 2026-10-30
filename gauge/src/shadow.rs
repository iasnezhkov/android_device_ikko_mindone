// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use crate::{proto, sample, transport};
use std::collections::VecDeque;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::time::Duration;

pub const RING_CAPACITY: usize = 8640;
pub const LOG_LIMIT: u64 = 4 * 1024 * 1024;
pub const HEADER: &str = "acquired_ns,sequence,status,voltage_01mv,ptim_discharge_01ma,charge_in_uah,gm_capacity_pct,gm_voltage_uv,gm_current_ua,gm_charge_counter_uah,gm_status,gm_temp_01c,drift_uah,event\n";

pub fn period_seconds(value: &str) -> u64 {
    value.trim().parse::<u64>().unwrap_or(10).clamp(1, 300)
}

pub trait Clock {
    fn now_ns(&self) -> io::Result<u64>;
}

#[derive(Clone, Debug, PartialEq)]
pub enum Value<T> {
    Ok(T),
    Err(String),
}

impl<T: std::fmt::Display> std::fmt::Display for Value<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Ok(value) => write!(f, "{}", cell(&value.to_string())),
            Self::Err(error) => write!(f, "err:{}", cell(error)),
        }
    }
}

fn cell(value: &str) -> String {
    value
        .chars()
        .take(160)
        .map(|c| {
            if c.is_ascii_alphanumeric() || "._:+- /()".contains(c) {
                c
            } else {
                '_'
            }
        })
        .collect()
}

fn read_value<T: std::str::FromStr>(path: &Path) -> Value<T> {
    let mut bytes = Vec::new();
    let result = File::open(path).and_then(|file| file.take(257).read_to_end(&mut bytes));
    if let Err(error) = result {
        return Value::Err(format!("errno:{}", error.raw_os_error().unwrap_or(-1)));
    }
    if bytes.len() > 256 {
        return Value::Err("oversized".into());
    }
    match std::str::from_utf8(&bytes)
        .ok()
        .and_then(|s| s.trim().parse().ok())
    {
        Some(value) => Value::Ok(value),
        None => Value::Err("invalid".into()),
    }
}

#[derive(Clone, Debug)]
pub struct Gm30 {
    pub capacity: Value<i64>,
    pub voltage: Value<i64>,
    pub current: Value<i64>,
    pub charge_counter: Value<i64>,
    pub status: Value<String>,
    pub temp: Value<i64>,
}

impl Gm30 {
    pub fn read(root: &Path) -> Self {
        Self {
            capacity: read_value(&root.join("capacity")),
            voltage: read_value(&root.join("voltage_now")),
            current: read_value(&root.join("current_now")),
            charge_counter: read_value(&root.join("charge_counter")),
            status: read_value(&root.join("status")),
            temp: read_value(&root.join("temp")),
        }
    }
}

#[derive(Default)]
pub struct Integrator {
    previous: Option<sample::Sample>,
    pub charge_in_uah: f64,
}

impl Integrator {
    pub fn push(&mut self, sample: sample::Sample) {
        let wire = sample.wire();
        if let Some(previous) = self.previous {
            let previous = previous.wire();
            if let Some(dt) = wire.acquired_ns.checked_sub(previous.acquired_ns) {
                self.charge_in_uah -=
                    (f64::from(previous.current_01ma) + f64::from(wire.current_01ma)) * dt as f64
                        / 72_000_000_000.0;
            }
        }
        self.previous = Some(sample);
    }

    pub fn interrupt(&mut self) {
        self.previous = None;
    }
}

pub struct Backoff {
    period: u64,
    errors: u32,
}

impl Backoff {
    pub fn new(period: u64) -> Self {
        Self {
            period: period.clamp(1, 300),
            errors: 0,
        }
    }

    pub fn record(&mut self, success: bool) -> Duration {
        self.errors = if success {
            0
        } else {
            self.errors.saturating_add(1)
        };
        Duration::from_secs(
            self.period
                .saturating_mul(1u64 << self.errors.min(9))
                .min(300),
        )
    }
}

#[derive(Clone, Debug)]
pub struct Row {
    pub acquired_ns: u64,
    pub sequence: u64,
    pub status: String,
    pub voltage: Option<i32>,
    pub current: Option<i32>,
    pub charge_in_uah: f64,
    pub gm: Gm30,
    pub drift_uah: Option<f64>,
    pub gap: bool,
}

impl Row {
    pub fn line(&self) -> String {
        let optional =
            |v: Option<i32>| v.map(|v| v.to_string()).unwrap_or_else(|| "unknown".into());
        format!(
            "{},{},{},{},{},{:.6},{},{},{},{},{},{},{},{}\n",
            self.acquired_ns,
            self.sequence,
            cell(&self.status),
            optional(self.voltage),
            optional(self.current),
            self.charge_in_uah,
            self.gm.capacity,
            self.gm.voltage,
            self.gm.current,
            self.gm.charge_counter,
            self.gm.status,
            self.gm.temp,
            self.drift_uah
                .map(|v| format!("{v:.6}"))
                .unwrap_or_else(|| "unknown".into()),
            if self.gap {
                "gap_gt_2_periods:sampling_paused_suspend_or_delay"
            } else {
                "none"
            }
        )
    }
}

pub struct Shadow {
    period: u64,
    sequence: u32,
    cache: sample::SampleCache,
    integrator: Integrator,
    anchor: Option<(i64, f64)>,
    last_acquired: Option<u64>,
    last_observed: Option<u64>,
    pub rows: VecDeque<Row>,
    pub backoff: Backoff,
}

impl Shadow {
    pub fn new(period: u64) -> Self {
        let period = period.clamp(1, 300);
        Self {
            period,
            sequence: 0,
            cache: sample::SampleCache::new(Duration::from_secs(period * 2)),
            integrator: Integrator::default(),
            anchor: None,
            last_acquired: None,
            last_observed: None,
            rows: VecDeque::with_capacity(RING_CAPACITY),
            backoff: Backoff::new(period),
        }
    }

    pub fn poll<T: transport::Transport, C: Clock>(
        &mut self,
        transport: &mut T,
        clock: &C,
        gm: Gm30,
    ) -> io::Result<Duration> {
        self.sequence = self.sequence.wrapping_add(1).max(1);
        let timeout = Duration::from_secs(1);
        let result = transport
            .send_request(&proto::ptim_request(), self.sequence, timeout)
            .and_then(|()| transport.receive_reply(self.sequence, timeout));
        let now = clock.now_ns()?;
        let decoded = result
            .map_err(|e| format!("transport:{e}"))
            .and_then(|bytes| proto::decode_ptim_reply(&bytes).map_err(|e| format!("protocol:{e}")))
            .and_then(|wire| {
                if wire.acquired_ns > now {
                    return Err("sample:FutureSample".into());
                }
                if now - wire.acquired_ns > self.period * 2 * 1_000_000_000 {
                    return Err("sample:Stale".into());
                }
                self.cache.update(wire).map_err(|e| format!("sample:{e:?}"))
            });
        let success = decoded.is_ok();
        let mut row = Row {
            acquired_ns: now,
            sequence: u64::from(self.sequence),
            status: "ok".into(),
            voltage: None,
            current: None,
            charge_in_uah: self.integrator.charge_in_uah,
            gm,
            drift_uah: None,
            gap: self
                .last_observed
                .is_some_and(|last| now.saturating_sub(last) > self.period * 2 * 1_000_000_000),
        };
        self.last_observed = Some(now);
        match decoded {
            Ok(sample) => {
                let wire = sample.wire();
                let resumed = self.last_acquired.is_some_and(|last| {
                    wire.acquired_ns.saturating_sub(last) > self.period * 2 * 1_000_000_000
                });
                row.gap |= resumed;
                if resumed {
                    self.integrator.interrupt();
                    self.anchor = None;
                }
                self.last_acquired = Some(wire.acquired_ns);
                self.integrator.push(sample);
                row.acquired_ns = wire.acquired_ns;
                row.sequence = wire.sequence;
                row.voltage = Some(wire.voltage_01mv);
                row.current = Some(wire.current_01ma);
                row.charge_in_uah = self.integrator.charge_in_uah;
                if let Value::Ok(counter) = row.gm.charge_counter {
                    let (initial, charge) =
                        *self.anchor.get_or_insert((counter, row.charge_in_uah));
                    row.drift_uah =
                        Some((row.charge_in_uah - charge) - (counter as f64 - initial as f64));
                }
            }
            Err(error) => {
                row.status = error;
                self.integrator.interrupt();
                self.anchor = None;
            }
        }
        if self.rows.len() == RING_CAPACITY {
            self.rows.pop_front();
        }
        self.rows.push_back(row);
        Ok(self.backoff.record(success))
    }
}

pub struct Log {
    path: PathBuf,
    limit: u64,
}

impl Log {
    pub fn new(root: &Path, limit: u64) -> io::Result<Self> {
        let log = Self {
            path: root.join("shadow.csv"),
            limit,
        };
        let rotated = log.path.with_extension("csv.1");
        if log.path.try_exists()? || rotated.try_exists()? {
            for archive in ["csv.previous", "csv.previous.1"] {
                match fs::remove_file(log.path.with_extension(archive)) {
                    Ok(()) => {}
                    Err(error) if error.kind() == io::ErrorKind::NotFound => {}
                    Err(error) => return Err(error),
                }
            }
            for (source, archive) in [(&log.path, "csv.previous"), (&rotated, "csv.previous.1")] {
                match fs::rename(source, log.path.with_extension(archive)) {
                    Ok(()) => {}
                    Err(error) if error.kind() == io::ErrorKind::NotFound => {}
                    Err(error) => return Err(error),
                }
            }
            File::open(root)?.sync_all()?;
        }
        Ok(log)
    }

    pub fn append(&self, row: &Row) -> io::Result<()> {
        self.append_line(&row.line())
    }

    pub fn append_line(&self, line: &str) -> io::Result<()> {
        if line.len() as u64 + HEADER.len() as u64 > self.limit {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "log line exceeds limit",
            ));
        }
        let mut options = OpenOptions::new();
        options.append(true).create(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let mut file = options.open(&self.path)?;
        let mut size = file.metadata()?.len();
        if size + line.len() as u64 > self.limit {
            file.sync_all()?;
            fs::rename(&self.path, self.path.with_extension("csv.1"))?;
            File::open(self.path.parent().unwrap())?.sync_all()?;
            file = options.open(&self.path)?;
            size = 0;
        }
        let bytes = if size == 0 {
            format!("{HEADER}{line}").into_bytes()
        } else {
            line.as_bytes().to_vec()
        };
        let written = file.write(&bytes)?;
        if written != bytes.len() {
            file.set_len(size)?;
            file.sync_data()?;
            return Err(io::Error::new(io::ErrorKind::WriteZero, "short log write"));
        }
        file.sync_data()?;
        if size == 0 {
            File::open(self.path.parent().unwrap())?.sync_all()?;
        }
        Ok(())
    }
}
