// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use std::collections::VecDeque;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::sync::Mutex;

use mindone_vsim_core::protocol::*;
use mindone_vsim_core::state::*;

const SOCKET_NAME: &str = "vsim-adaptor";
const RECONNECT_BACKOFF_MS: u64 = 2_000;
const AID_ROOT: u32 = 0;
const AID_RADIO: u32 = 1001;
const AID_SHELL: u32 = 2000;
const AID_APP_START: u32 = 10000;

#[repr(C)]
struct BinderCallbacks {
    ctx: *mut c_void,
    on_notif_rsp: extern "C" fn(*mut c_void, i32, i32),
    on_op_rsp: extern "C" fn(*mut c_void, i32, i32),
    on_event_ind: extern "C" fn(*mut c_void, i32, i32, i32, i32, *const c_char),
    on_status_ind: extern "C" fn(*mut c_void, i32),
    on_ril_state: extern "C" fn(*mut c_void, i32),
}

extern "C" {
    fn vsim_binder_init(callbacks: *const BinderCallbacks) -> c_int;
    fn vsim_binder_connect() -> c_int;
    fn vsim_binder_send_notification(serial: i32, tx_id: i32, event_id: i32, sim_type: i32) -> c_int;
    fn vsim_binder_send_operation(
        serial: i32,
        tx_id: i32,
        event_id: i32,
        result: i32,
        data: *const u8,
        data_len: usize,
    ) -> c_int;
    fn vsim_binder_shutdown();

    fn vsim_net_listen_abstract(name: *const c_char) -> c_int;
    fn vsim_net_accept(
        listen_fd: c_int,
        pid: *mut u32,
        uid: *mut u32,
        gid: *mut u32,
        secctx: *mut c_char,
        secctx_cap: usize,
    ) -> c_int;
    fn vsim_net_read(fd: c_int, buf: *mut u8, cap: usize) -> c_int;
    fn vsim_net_write_all(fd: c_int, buf: *const u8, len: usize) -> c_int;
    fn vsim_net_close(fd: c_int);
    fn vsim_net_pipe(fds: *mut c_int) -> c_int;
    fn vsim_net_wake(write_fd: c_int);
    fn vsim_net_drain(read_fd: c_int);
    fn vsim_net_poll(
        listen_fd: c_int,
        client_fd: c_int,
        wake_fd: c_int,
        sig_fd: c_int,
        timeout_ms: c_int,
        mask: *mut u32,
    ) -> c_int;
    fn vsim_net_now_ms() -> u64;
    fn vsim_net_install_signals(sig_write_fd: c_int) -> c_int;
    fn vsim_prop_get(name: *const c_char, out: *mut c_char, cap: usize) -> c_int;
    fn vsim_log_msg(level: c_int, msg: *const c_char);
}

enum BinderEvent {
    NotifResponse { serial: i32, radio_error: i32 },
    OpResponse { serial: i32, radio_error: i32 },
    EventInd { tx_id: i32, event_id: i32, hex_data: String },
    StatusInd { sim_inserted: i32 },
    RilState { connected: bool },
}

struct SharedQueue {
    events: Mutex<VecDeque<BinderEvent>>,
    wake_fd: c_int,
}

impl SharedQueue {
    fn push(&self, ev: BinderEvent) {
        if let Ok(mut guard) = self.events.lock() {
            guard.push_back(ev);
        }
        unsafe { vsim_net_wake(self.wake_fd) };
    }

    fn drain(&self) -> Vec<BinderEvent> {
        let mut out = Vec::new();
        if let Ok(mut guard) = self.events.lock() {
            while let Some(ev) = guard.pop_front() {
                out.push(ev);
            }
        }
        out
    }
}

fn log_info(msg: &str) {
    if let Ok(c) = CString::new(msg) {
        unsafe { vsim_log_msg(0, c.as_ptr()) };
    }
}

fn log_warn(msg: &str) {
    if let Ok(c) = CString::new(msg) {
        unsafe { vsim_log_msg(1, c.as_ptr()) };
    }
}

fn log_err(msg: &str) {
    if let Ok(c) = CString::new(msg) {
        unsafe { vsim_log_msg(2, c.as_ptr()) };
    }
}

fn get_prop(name: &str) -> String {
    let Ok(cname) = CString::new(name) else {
        return String::new();
    };
    let mut buf = [0u8; 128];
    let n = unsafe { vsim_prop_get(cname.as_ptr(), buf.as_mut_ptr() as *mut c_char, buf.len()) };
    if n <= 0 {
        return String::new();
    }
    let len = (n as usize).min(buf.len() - 1);
    String::from_utf8_lossy(&buf[..len]).into_owned()
}

fn read_modem_vsim_capability() -> i32 {
    let raw = get_prop("vendor.gsm.modem.vsim.capability");
    if raw.is_empty() {
        return 0;
    }
    raw.split(',')
        .next()
        .and_then(|s| s.trim().parse::<i32>().ok())
        .unwrap_or(0)
}

fn peer_allowed(uid: u32, secctx: &str) -> bool {
    if secctx.is_empty() {
        return false;
    }
    let debuggable = get_prop("ro.debuggable") == "1";
    if uid == AID_RADIO {
        return true;
    }
    if debuggable && (uid == AID_SHELL || uid == AID_ROOT) {
        return true;
    }
    let pinned_uid = get_prop("persist.sys.mindone.vsim.uid");
    if let Ok(want) = pinned_uid.trim().parse::<u32>() {
        return want >= AID_APP_START && uid == want;
    }
    false
}

extern "C" fn cb_notif_rsp(ctx: *mut c_void, serial: i32, radio_error: i32) {
    if ctx.is_null() {
        return;
    }
    let q = unsafe { &*(ctx as *const SharedQueue) };
    q.push(BinderEvent::NotifResponse { serial, radio_error });
}

extern "C" fn cb_op_rsp(ctx: *mut c_void, serial: i32, radio_error: i32) {
    if ctx.is_null() {
        return;
    }
    let q = unsafe { &*(ctx as *const SharedQueue) };
    q.push(BinderEvent::OpResponse { serial, radio_error });
}

extern "C" fn cb_event_ind(
    ctx: *mut c_void,
    tx_id: i32,
    event_id: i32,
    _result: i32,
    _data_len: i32,
    hex_data: *const c_char,
) {
    if ctx.is_null() {
        return;
    }
    let hex = if hex_data.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(hex_data) }
            .to_string_lossy()
            .into_owned()
    };
    let q = unsafe { &*(ctx as *const SharedQueue) };
    q.push(BinderEvent::EventInd {
        tx_id,
        event_id,
        hex_data: hex,
    });
}

extern "C" fn cb_status_ind(ctx: *mut c_void, sim_inserted: i32) {
    if ctx.is_null() {
        return;
    }
    let q = unsafe { &*(ctx as *const SharedQueue) };
    q.push(BinderEvent::StatusInd { sim_inserted });
}

extern "C" fn cb_ril_state(ctx: *mut c_void, connected: i32) {
    if ctx.is_null() {
        return;
    }
    let q = unsafe { &*(ctx as *const SharedQueue) };
    q.push(BinderEvent::RilState {
        connected: connected != 0,
    });
}

struct Daemon {
    engine: VsimEngine,
    client_fd: c_int,
    rx_buf: Vec<u8>,
    ril_connected: bool,
    next_reconnect_ms: u64,
}

impl Daemon {
    fn new() -> Self {
        let cap = read_modem_vsim_capability();
        Self {
            engine: VsimEngine::new(cap),
            client_fd: -1,
            rx_buf: Vec::with_capacity(HEADER_LEN + MAX_PAYLOAD_LEN),
            ril_connected: false,
            next_reconnect_ms: 0,
        }
    }

    fn execute(&mut self, actions: Vec<Action>) {
        for action in actions {
            match action {
                Action::SendClient(frame) => {
                    if self.client_fd < 0 {
                        continue;
                    }
                    match frame.encode() {
                        Ok(bytes) => {
                            let rc = unsafe {
                                vsim_net_write_all(self.client_fd, bytes.as_ptr(), bytes.len())
                            };
                            if rc != 0 {
                                log_warn("socket write failed, tearing down client");
                                self.drop_client();
                            }
                        }
                        Err(_) => {
                            log_err("failed to encode outgoing frame");
                            self.drop_client();
                        }
                    }
                }
                Action::CloseClient => {
                    if self.client_fd >= 0 {
                        unsafe { vsim_net_close(self.client_fd) };
                        self.client_fd = -1;
                        self.rx_buf.clear();
                    }
                }
                Action::SendVsimNotification {
                    serial,
                    tx_id,
                    event_id,
                    sim_type,
                } => {
                    if unsafe {
                        vsim_binder_send_notification(serial, tx_id, event_id, sim_type)
                    } != 0
                    {
                        log_warn(&format!(
                            "sendVsimNotification(serial={}, event={}) failed",
                            serial, event_id
                        ));
                    }
                }
                Action::SendVsimOperation {
                    serial,
                    tx_id,
                    event_id,
                    result,
                    data,
                } => {
                    let ptr = if data.is_empty() {
                        std::ptr::null()
                    } else {
                        data.as_ptr()
                    };
                    if unsafe {
                        vsim_binder_send_operation(serial, tx_id, event_id, result, ptr, data.len())
                    } != 0
                    {
                        log_warn(&format!(
                            "sendVsimOperation(serial={}, event={}) failed",
                            serial, event_id
                        ));
                    }
                }
            }
        }
    }

    fn drop_client(&mut self) {
        if self.client_fd >= 0 {
            unsafe { vsim_net_close(self.client_fd) };
            self.client_fd = -1;
            self.rx_buf.clear();
        }
        let actions = self.engine.on_client_disconnected();
        self.execute(actions);
    }

    fn accept_client(&mut self, listen_fd: c_int) {
        loop {
            let mut pid = 0u32;
            let mut uid = 0u32;
            let mut gid = 0u32;
            let mut secbuf = [0u8; 256];
            let fd = unsafe {
                vsim_net_accept(
                    listen_fd,
                    &mut pid,
                    &mut uid,
                    &mut gid,
                    secbuf.as_mut_ptr() as *mut c_char,
                    secbuf.len(),
                )
            };
            if fd == -2 {
                break;
            }
            if fd < 0 {
                break;
            }
            let nul = secbuf.iter().position(|&b| b == 0).unwrap_or(secbuf.len());
            let secctx = String::from_utf8_lossy(&secbuf[..nul]).into_owned();
            if !peer_allowed(uid, &secctx) {
                log_warn(&format!(
                    "rejected unauthorized peer pid={} uid={} gid={} ctx={}",
                    pid, uid, gid, secctx
                ));
                unsafe { vsim_net_close(fd) };
                continue;
            }
            if self.client_fd >= 0 {
                log_warn(&format!(
                    "rejected concurrent peer pid={} uid={} while active session exists",
                    pid, uid
                ));
                unsafe { vsim_net_close(fd) };
                continue;
            }
            self.engine.set_modem_capability(read_modem_vsim_capability());
            self.client_fd = fd;
            self.rx_buf.clear();
            log_info(&format!(
                "accepted peer pid={} uid={} gid={} ctx={}",
                pid, uid, gid, secctx
            ));
            let actions = self.engine.on_client_connected();
            self.execute(actions);
        }
    }

    fn read_client(&mut self) {
        if self.client_fd < 0 {
            return;
        }
        let mut chunk = [0u8; HEADER_LEN + MAX_PAYLOAD_LEN];
        loop {
            let n = unsafe { vsim_net_read(self.client_fd, chunk.as_mut_ptr(), chunk.len()) };
            if n == -2 {
                break;
            }
            if n <= 0 {
                log_info("client disconnected");
                self.drop_client();
                return;
            }
            self.rx_buf.extend_from_slice(&chunk[..n as usize]);
            if self.rx_buf.len() > (HEADER_LEN + MAX_PAYLOAD_LEN) * 4 {
                log_warn("client receive buffer overflow, closing");
                self.drop_client();
                return;
            }
            if !self.process_rx_frames() {
                return;
            }
        }
    }

    fn process_rx_frames(&mut self) -> bool {
        while self.rx_buf.len() >= HEADER_LEN {
            let mut hdr = [0u8; HEADER_LEN];
            hdr.copy_from_slice(&self.rx_buf[..HEADER_LEN]);
            let (tx_id, msg_id, slot_id, payload_len) = match Frame::decode_header(&hdr) {
                Ok(parsed) => parsed,
                Err(_) => {
                    log_warn("malformed frame header from client, tearing down");
                    self.drop_client();
                    return false;
                }
            };
            let total = HEADER_LEN + payload_len;
            if self.rx_buf.len() < total {
                break;
            }
            let payload = self.rx_buf[HEADER_LEN..total].to_vec();
            self.rx_buf.drain(..total);
            if msg_id == MSG_ID_GET_PLATFORM_CAPABILITY {
                self.engine.set_modem_capability(read_modem_vsim_capability());
            }
            let actions = self.engine.on_client_frame(Frame {
                tx_id,
                msg_id,
                slot_id,
                payload,
            });
            self.execute(actions);
            if self.client_fd < 0 {
                return false;
            }
        }
        true
    }

    fn handle_binder_event(&mut self, ev: BinderEvent, now_ms: u64) {
        match ev {
            BinderEvent::NotifResponse {
                serial,
                radio_error,
            } => {
                let actions = self.engine.on_ril_notification_response(serial, radio_error);
                self.execute(actions);
            }
            BinderEvent::OpResponse {
                serial,
                radio_error,
            } => {
                if radio_error != 0 {
                    log_warn(&format!(
                        "vsimOperationResponse(serial={}) error={}",
                        serial, radio_error
                    ));
                }
            }
            BinderEvent::EventInd {
                tx_id,
                event_id,
                hex_data,
            } => {
                let actions = self
                    .engine
                    .on_ril_event_indication(tx_id, event_id, &hex_data, now_ms);
                self.execute(actions);
            }
            BinderEvent::StatusInd { sim_inserted } => {
                log_info(&format!("virtual SIM status changed: {}", sim_inserted));
            }
            BinderEvent::RilState { connected } => {
                self.ril_connected = connected;
                if connected {
                    self.engine.set_modem_capability(read_modem_vsim_capability());
                } else {
                    let actions = self.engine.on_ril_died();
                    self.execute(actions);
                    self.next_reconnect_ms = now_ms.saturating_add(RECONNECT_BACKOFF_MS);
                }
            }
        }
    }

    fn poll_timeout_ms(&self, now_ms: u64) -> c_int {
        let mut target: Option<u64> = self.engine.next_deadline_ms();
        if !self.ril_connected {
            let r = self.next_reconnect_ms;
            target = Some(match target {
                Some(t) => t.min(r),
                None => r,
            });
        }
        match target {
            None => -1,
            Some(deadline) => {
                let delta = deadline.saturating_sub(now_ms);
                delta.min(i32::MAX as u64) as c_int
            }
        }
    }
}

fn main() {
    let mut wake_pipe = [-1 as c_int; 2];
    let mut sig_pipe = [-1 as c_int; 2];
    if unsafe { vsim_net_pipe(wake_pipe.as_mut_ptr()) } != 0
        || unsafe { vsim_net_pipe(sig_pipe.as_mut_ptr()) } != 0
    {
        log_err("failed to create event pipes");
        std::process::exit(1);
    }
    if unsafe { vsim_net_install_signals(sig_pipe[1]) } != 0 {
        log_err("failed to install signal handlers");
        std::process::exit(1);
    }

    let sock_name = CString::new(SOCKET_NAME).unwrap();
    let listen_fd = unsafe { vsim_net_listen_abstract(sock_name.as_ptr()) };
    if listen_fd < 0 {
        log_err("failed to listen on @vsim-adaptor");
        std::process::exit(1);
    }

    let queue = Box::new(SharedQueue {
        events: Mutex::new(VecDeque::new()),
        wake_fd: wake_pipe[1],
    });
    let queue_ptr = Box::into_raw(queue);

    let callbacks = BinderCallbacks {
        ctx: queue_ptr as *mut c_void,
        on_notif_rsp: cb_notif_rsp,
        on_op_rsp: cb_op_rsp,
        on_event_ind: cb_event_ind,
        on_status_ind: cb_status_ind,
        on_ril_state: cb_ril_state,
    };
    if unsafe { vsim_binder_init(&callbacks) } != 0 {
        log_err("vsim_binder_init failed");
        std::process::exit(1);
    }

    let mut daemon = Daemon::new();
    let now = unsafe { vsim_net_now_ms() };
    if unsafe { vsim_binder_connect() } == 0 {
        daemon.ril_connected = true;
    } else {
        daemon.next_reconnect_ms = now.saturating_add(RECONNECT_BACKOFF_MS);
    }
    log_info("vsim-adaptor ready on @vsim-adaptor");

    loop {
        let now_ms = unsafe { vsim_net_now_ms() };
        if !daemon.ril_connected && now_ms >= daemon.next_reconnect_ms {
            if unsafe { vsim_binder_connect() } == 0 {
                daemon.ril_connected = true;
            } else {
                daemon.next_reconnect_ms = now_ms.saturating_add(RECONNECT_BACKOFF_MS);
            }
        }

        let tick_actions = daemon.engine.on_tick(now_ms);
        daemon.execute(tick_actions);

        let timeout = daemon.poll_timeout_ms(now_ms);
        let mut mask = 0u32;
        let rc = unsafe {
            vsim_net_poll(
                listen_fd,
                daemon.client_fd,
                wake_pipe[0],
                sig_pipe[0],
                timeout,
                &mut mask,
            )
        };
        if rc < 0 {
            log_err("poll failed, exiting");
            break;
        }
        if (mask & (1u32 << 3)) != 0 {
            unsafe { vsim_net_drain(sig_pipe[0]) };
            log_info("shutdown signal received, restoring physical SIM");
            break;
        }
        if (mask & (1u32 << 2)) != 0 {
            unsafe { vsim_net_drain(wake_pipe[0]) };
            let evs = unsafe { &*queue_ptr }.drain();
            let ev_now = unsafe { vsim_net_now_ms() };
            for ev in evs {
                daemon.handle_binder_event(ev, ev_now);
            }
        }
        if (mask & (1u32 << 0)) != 0 {
            daemon.accept_client(listen_fd);
        }
        if (mask & (1u32 << 1)) != 0 {
            daemon.read_client();
        }
    }

    let shutdown_actions = daemon.engine.on_shutdown();
    daemon.execute(shutdown_actions);
    unsafe {
        vsim_binder_shutdown();
        vsim_net_close(listen_fd);
        vsim_net_close(wake_pipe[0]);
        vsim_net_close(wake_pipe[1]);
        vsim_net_close(sig_pipe[0]);
        vsim_net_close(sig_pipe[1]);
    }
}
