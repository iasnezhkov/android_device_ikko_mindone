// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use crate::proto::{u32_at, u64_at};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

pub const MAGIC: [u8; 8] = *b"M1GAUGE\0";
pub const VERSION: u32 = 1;
pub const PAYLOAD_LEN: usize = 24;
pub const RECORD_LEN: usize = 44;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ValueError {
    ZeroQmax,
    Permille,
    ZeroSampleSequence,
}

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    DurabilityAfterRename(io::Error),
    Length,
    Magic,
    Version(u32),
    Crc,
    Value(ValueError),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}

impl std::error::Error for Error {}

impl From<io::Error> for Error {
    fn from(value: io::Error) -> Self {
        Self::Io(value)
    }
}

impl From<ValueError> for Error {
    fn from(value: ValueError) -> Self {
        Self::Value(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MicroampHours(u32);

impl MicroampHours {
    pub fn new(value: u32) -> Result<Self, ValueError> {
        if value == 0 {
            Err(ValueError::ZeroQmax)
        } else {
            Ok(Self(value))
        }
    }

    pub fn get(self) -> u32 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Permille(u32);

impl Permille {
    pub fn new(value: u32) -> Result<Self, ValueError> {
        if value > 1000 {
            Err(ValueError::Permille)
        } else {
            Ok(Self(value))
        }
    }

    pub fn get(self) -> u32 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CycleCount(pub u32);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SampleSequence(u64);

impl SampleSequence {
    pub fn new(value: u64) -> Result<Self, ValueError> {
        if value == 0 {
            Err(ValueError::ZeroSampleSequence)
        } else {
            Ok(Self(value))
        }
    }

    pub fn get(self) -> u64 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LearnedState {
    pub qmax_uah: MicroampHours,
    pub aging_factor_permille: Permille,
    pub cycle_count: CycleCount,
    pub last_soc_permille: Permille,
    pub last_soc_sample_sequence: SampleSequence,
}

pub fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}

impl LearnedState {
    pub fn encode(self) -> [u8; RECORD_LEN] {
        let mut out = [0; RECORD_LEN];
        out[..8].copy_from_slice(&MAGIC);
        for (offset, value) in [
            (8, VERSION),
            (12, PAYLOAD_LEN as u32),
            (16, self.qmax_uah.get()),
            (20, self.aging_factor_permille.get()),
            (24, self.cycle_count.0),
            (28, self.last_soc_permille.get()),
        ] {
            out[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        out[32..40].copy_from_slice(&self.last_soc_sample_sequence.get().to_le_bytes());
        let crc = crc32(&out[..40]);
        out[40..].copy_from_slice(&crc.to_le_bytes());
        out
    }

    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        if bytes.len() != RECORD_LEN {
            return Err(Error::Length);
        }
        if bytes[..8] != MAGIC {
            return Err(Error::Magic);
        }
        if u32_at(bytes, 8) != VERSION {
            return Err(Error::Version(u32_at(bytes, 8)));
        }
        if u32_at(bytes, 12) != PAYLOAD_LEN as u32 {
            return Err(Error::Length);
        }
        if crc32(&bytes[..40]) != u32_at(bytes, 40) {
            return Err(Error::Crc);
        }
        Ok(Self {
            qmax_uah: MicroampHours::new(u32_at(bytes, 16))?,
            aging_factor_permille: Permille::new(u32_at(bytes, 20))?,
            cycle_count: CycleCount(u32_at(bytes, 24)),
            last_soc_permille: Permille::new(u32_at(bytes, 28))?,
            last_soc_sample_sequence: SampleSequence::new(u64_at(bytes, 32))?,
        })
    }
}

pub fn read(path: &Path) -> Result<LearnedState, Error> {
    let mut bytes = Vec::with_capacity(RECORD_LEN + 1);
    File::open(path)?
        .take((RECORD_LEN + 1) as u64)
        .read_to_end(&mut bytes)?;
    LearnedState::decode(&bytes)
}

struct Temporary(PathBuf);

impl Drop for Temporary {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.0);
    }
}

pub fn write_atomic(path: &Path, state: LearnedState) -> Result<(), Error> {
    write_atomic_inner(path, &state.encode(), || Ok(()))
}

pub(crate) fn write_atomic_inner(
    path: &Path,
    bytes: &[u8],
    before_rename: impl FnOnce() -> io::Result<()>,
) -> Result<(), Error> {
    static NEXT_TEMP: AtomicU64 = AtomicU64::new(0);
    let name = path
        .file_name()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "missing filename"))?;
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let directory = File::open(parent)?;
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let (temporary, mut file) = (0..128)
        .find_map(|_| {
            let mut temporary_name = name.to_os_string();
            temporary_name.push(format!(
                ".tmp.{}.{}",
                std::process::id(),
                NEXT_TEMP.fetch_add(1, Ordering::Relaxed)
            ));
            let temporary_path = parent.join(temporary_name);
            match options.open(&temporary_path) {
                Ok(file) => Some(Ok((Temporary(temporary_path), file))),
                Err(error) if error.kind() == io::ErrorKind::AlreadyExists => None,
                Err(error) => Some(Err(error)),
            }
        })
        .unwrap_or_else(|| {
            Err(io::Error::new(
                io::ErrorKind::AlreadyExists,
                "temporary name exhaustion",
            ))
        })?;
    file.write_all(bytes)?;
    file.sync_all()?;
    drop(file);
    before_rename()?;
    fs::rename(&temporary.0, path)?;
    directory.sync_all().map_err(Error::DurabilityAfterRename)?;
    Ok(())
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ShadowState {
    pub qmax_uah: Option<MicroampHours>,
    pub aging_factor_permille: Option<Permille>,
    pub cycle_count: Option<CycleCount>,
    pub charge_in_uah: f64,
    pub acquired_ns: u64,
    pub sequence: u64,
}

impl ShadowState {
    pub fn encode(self) -> Result<[u8; 68], Error> {
        if !self.charge_in_uah.is_finite() {
            return Err(Error::Length);
        }
        let mut out = [0; 68];
        out[..8].copy_from_slice(&MAGIC);
        for (offset, value) in [
            (8, 3u32),
            (12, 48),
            (
                16,
                u32::from(self.qmax_uah.is_some())
                    | (u32::from(self.aging_factor_permille.is_some()) << 1)
                    | (u32::from(self.cycle_count.is_some()) << 2),
            ),
            (20, self.qmax_uah.map(MicroampHours::get).unwrap_or(0)),
            (
                24,
                self.aging_factor_permille.map(Permille::get).unwrap_or(0),
            ),
            (28, self.cycle_count.map(|v| v.0).unwrap_or(0)),
        ] {
            out[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        out[32..40].copy_from_slice(&self.charge_in_uah.to_le_bytes());
        out[40..48].copy_from_slice(&self.acquired_ns.to_le_bytes());
        out[48..56].copy_from_slice(&self.sequence.to_le_bytes());
        let crc = crc32(&out[..64]);
        out[64..].copy_from_slice(&crc.to_le_bytes());
        Ok(out)
    }

    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        if bytes.len() != 68 {
            return Err(Error::Length);
        }
        if bytes[..8] != MAGIC {
            return Err(Error::Magic);
        }
        if u32_at(bytes, 8) != 3 {
            return Err(Error::Version(u32_at(bytes, 8)));
        }
        if u32_at(bytes, 12) != 48 || u32_at(bytes, 16) & !7 != 0 || bytes[56..64] != [0; 8] {
            return Err(Error::Length);
        }
        if crc32(&bytes[..64]) != u32_at(bytes, 64) {
            return Err(Error::Crc);
        }
        let flags = u32_at(bytes, 16);
        for (bit, offset) in [(1, 20), (2, 24), (4, 28)] {
            if flags & bit == 0 && u32_at(bytes, offset) != 0 {
                return Err(Error::Length);
            }
        }
        let charge_in_uah = f64::from_le_bytes(bytes[32..40].try_into().unwrap());
        if !charge_in_uah.is_finite() {
            return Err(Error::Length);
        }
        Ok(Self {
            qmax_uah: if flags & 1 != 0 {
                Some(MicroampHours::new(u32_at(bytes, 20))?)
            } else {
                None
            },
            aging_factor_permille: if flags & 2 != 0 {
                Some(Permille::new(u32_at(bytes, 24))?)
            } else {
                None
            },
            cycle_count: if flags & 4 != 0 {
                Some(CycleCount(u32_at(bytes, 28)))
            } else {
                None
            },
            charge_in_uah,
            acquired_ns: u64_at(bytes, 40),
            sequence: u64_at(bytes, 48),
        })
    }
}

pub fn write_shadow_atomic(path: &Path, state: ShadowState) -> Result<(), Error> {
    write_atomic_inner(path, &state.encode()?, || Ok(()))
}
