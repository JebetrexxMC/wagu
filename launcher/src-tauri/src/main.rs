// MP Launcher for PC: SA-MP / open.mp server browser with MP Crossplay 24/7 on top.
// Built on Tauri (JebetrexxMC/tauri fork). The join flow follows the open.mp launcher
// (github.com/openmultiplayer/launcher): start gta_sa.exe -c -n NAME -h IP -p PORT [-z PASS],
// then load samp.dll and, when it is there, the open.mp client DLL into the game.
// No open.mp code, name or artwork is used (MPL-2.0 + their trademark terms).
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::Serialize;
use serde_json::Value;
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::time::{Duration, Instant};

const OMP_LIST: &str = "https://api.open.mp/servers";
const WIRES: &str = "https://raw.githubusercontent.com/JebetrexxMC/wagu/netwire-status/wires.json";
const CROSSPLAY_FALLBACK: (&str, u16) = ("wap-fresh.tun.ply.gg", 56616);

#[derive(Serialize, Default)]
struct Info {
    ok: bool,
    ping: Option<u32>,
    password: bool,
    players: u16,
    max: u16,
    hostname: String,
    gamemode: String,
    language: String,
    error: String,
}

#[derive(Serialize)]
struct Featured {
    name: String,
    host: String,
    port: u16,
    note: String,
}

#[derive(Serialize, Default)]
struct SampSettings {
    gta_dir: String,
    name: String,
}

fn resolve(host: &str, port: u16) -> Result<SocketAddr, String> {
    (host, port)
        .to_socket_addrs()
        .map_err(|e| e.to_string())?
        .find(|a| a.is_ipv4())
        .ok_or_else(|| "no IPv4 address".into())
}

fn latin1(b: &[u8]) -> String {
    b.iter().map(|&c| c as char).collect()
}

// SA-MP query: "SAMP" + ip(4) + port(2 LE) + opcode [+ 4 bytes for 'p']
fn samp_ask(sock: &UdpSocket, a: &SocketAddr, op: u8) -> Option<(Duration, Vec<u8>)> {
    let SocketAddr::V4(v4) = a else { return None };
    let mut q = b"SAMP".to_vec();
    q.extend_from_slice(&v4.ip().octets());
    q.extend_from_slice(&v4.port().to_le_bytes());
    q.push(op);
    let tok: [u8; 4] = (std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.subsec_nanos()).unwrap_or(7) ^ 0x4d50_4c41).to_le_bytes();
    if op == b'p' {
        q.extend_from_slice(&tok);
    }
    let t0 = Instant::now();
    sock.send_to(&q, a).ok()?;
    let mut buf = [0u8; 2048];
    loop {
        let (n, from) = sock.recv_from(&mut buf).ok()?;
        let b = &buf[..n];
        if from == *a && n >= 11 && &b[..4] == b"SAMP" && b[10] == op && (op != b'p' || (n >= 15 && b[11..15] == tok)) {
            return Some((t0.elapsed(), b.to_vec()));
        }
        if t0.elapsed() > Duration::from_millis(1500) {
            return None;
        }
    }
}

fn query_blocking(host: &str, port: u16) -> Info {
    let mut r = Info::default();
    let a = match resolve(host, port) {
        Ok(a) => a,
        Err(e) => { r.error = e; return r; }
    };
    let sock = match UdpSocket::bind("0.0.0.0:0") {
        Ok(s) => s,
        Err(e) => { r.error = e.to_string(); return r; }
    };
    let _ = sock.set_read_timeout(Some(Duration::from_millis(1200)));
    let mut best: Option<Duration> = None;
    for _ in 0..3 {
        if let Some((d, _)) = samp_ask(&sock, &a, b'p') {
            best = Some(best.map_or(d, |b| b.min(d)));
        }
    }
    if let Some((d, b)) = samp_ask(&sock, &a, b'i') {
        best = Some(best.map_or(d, |x| x.min(d)));
        // 11: password(1) players(2) max(2) hostname(len4+s) gamemode(len4+s) language(len4+s)
        let mut p = 11usize;
        let rd16 = |b: &[u8], p: usize| b.get(p..p + 2).map(|x| u16::from_le_bytes([x[0], x[1]])).unwrap_or(0);
        let str32 = |b: &[u8], p: &mut usize| -> String {
            let Some(l) = b.get(*p..*p + 4) else { return String::new() };
            let l = u32::from_le_bytes([l[0], l[1], l[2], l[3]]) as usize;
            let s = b.get(*p + 4..*p + 4 + l).map(latin1).unwrap_or_default();
            *p += 4 + l;
            s
        };
        r.password = b.get(p).copied().unwrap_or(0) != 0;
        p += 1;
        r.players = rd16(&b, p);
        r.max = rd16(&b, p + 2);
        p += 4;
        r.hostname = str32(&b, &mut p);
        r.gamemode = str32(&b, &mut p);
        r.language = str32(&b, &mut p);
        r.ok = true;
    }
    r.ping = best.map(|d| d.as_millis() as u32);
    if !r.ok && r.ping.is_some() { r.ok = true; }
    if !r.ok { r.error = "no answer".into(); }
    r
}

#[tauri::command]
async fn query(host: String, port: u16) -> Info {
    tauri::async_runtime::spawn_blocking(move || query_blocking(&host, port)).await.unwrap_or_default()
}

fn get_json(url: &str) -> Result<Value, String> {
    ureq::get(url)
        .timeout(Duration::from_secs(15))
        .set("User-Agent", "mp-launcher/1.0")
        .call()
        .map_err(|e| e.to_string())?
        .into_json::<Value>()
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn servers() -> Result<Value, String> {
    tauri::async_runtime::spawn_blocking(|| get_json(OMP_LIST)).await.map_err(|e| e.to_string())?
}

#[tauri::command]
async fn featured() -> Vec<Featured> {
    let mut out = Vec::new();
    if let Ok(Ok(v)) = tauri::async_runtime::spawn_blocking(|| get_json(WIRES)).await {
        for w in v["wires"].as_array().into_iter().flatten() {
            if w["group"] == "crossplay" {
                if let (Some(h), Some(p)) = (w["host"].as_str(), w["port"].as_u64()) {
                    out.push(Featured {
                        name: w["name"].as_str().unwrap_or("MP Crossplay 24/7").into(),
                        host: h.into(),
                        port: p as u16,
                        note: "PC + Mobile: plays with MP for Android".into(),
                    });
                }
            }
        }
    }
    if out.is_empty() {
        out.push(Featured {
            name: "MP Crossplay 24/7 [PC + Mobile]".into(),
            host: CROSSPLAY_FALLBACK.0.into(),
            port: CROSSPLAY_FALLBACK.1,
            note: "PC + Mobile: plays with MP for Android".into(),
        });
    }
    out
}

#[tauri::command]
fn samp_settings() -> SampSettings {
    #[cfg(windows)]
    {
        use winreg::{enums::HKEY_CURRENT_USER, RegKey};
        if let Ok(k) = RegKey::predef(HKEY_CURRENT_USER).open_subkey("Software\\SAMP") {
            let exe: String = k.get_value("gta_sa_exe").unwrap_or_default();
            let name: String = k.get_value("PlayerName").unwrap_or_default();
            let dir = std::path::Path::new(&exe).parent().map(|p| p.display().to_string()).unwrap_or_default();
            return SampSettings { gta_dir: dir, name };
        }
    }
    SampSettings::default()
}

#[tauri::command]
fn check_game(gta_dir: String) -> Value {
    let d = std::path::Path::new(&gta_dir);
    serde_json::json!({
        "gta_sa": d.join("gta_sa.exe").is_file(),
        "samp": d.join("samp.dll").is_file(),
        "omp": omp_dll(d).is_some(),
    })
}

fn omp_dll(d: &std::path::Path) -> Option<std::path::PathBuf> {
    let mut c = vec![d.join("omp-client.dll")];
    if let Ok(l) = std::env::var("LOCALAPPDATA") {
        c.push(std::path::Path::new(&l).join("mp.open.launcher").join("omp").join("omp-client.dll"));
        c.push(std::path::Path::new(&l).join("MP Launcher").join("omp-client.dll"));
    }
    c.into_iter().find(|p| p.is_file())
}

#[tauri::command]
async fn play(host: String, port: u16, name: String, password: String, gta_dir: String) -> Result<String, String> {
    if name.trim().is_empty() { return Err("Set a nickname first".into()); }
    let ip = resolve(&host, port).map(|a| a.ip().to_string()).unwrap_or(host.clone());
    tauri::async_runtime::spawn_blocking(move || launch(&ip, port, name.trim(), &password, &gta_dir))
        .await
        .map_err(|e| e.to_string())?
}

#[cfg(windows)]
mod win {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Foundation::{CloseHandle, GetLastError, HANDLE, WAIT_OBJECT_0};
    use windows_sys::Win32::System::Diagnostics::Debug::WriteProcessMemory;
    use windows_sys::Win32::System::LibraryLoader::{GetModuleHandleW, GetProcAddress};
    use windows_sys::Win32::System::Memory::{VirtualAllocEx, VirtualFreeEx, MEM_COMMIT, MEM_RELEASE, MEM_RESERVE, PAGE_READWRITE};
    use windows_sys::Win32::System::Threading::{
        CreateProcessW, CreateRemoteThread, GetExitCodeThread, ResumeThread, TerminateProcess, WaitForSingleObject,
        CREATE_SUSPENDED, PROCESS_INFORMATION, STARTUPINFOW,
    };

    pub fn wide(s: &std::ffi::OsStr) -> Vec<u16> { s.encode_wide().chain(Some(0)).collect() }

    pub struct Game { pub pi: PROCESS_INFORMATION }

    impl Game {
        /// gta_sa.exe started suspended, so samp.dll is in before the game's first frame (like samp.exe)
        pub fn start(exe: &std::path::Path, dir: &std::path::Path, args: &str) -> Result<Game, String> {
            let mut cmdline = wide(std::ffi::OsStr::new(&format!("\"{}\" {}", exe.display(), args)));
            let exe_w = wide(exe.as_os_str());
            let dir_w = wide(dir.as_os_str());
            let mut si: STARTUPINFOW = unsafe { std::mem::zeroed() };
            si.cb = std::mem::size_of::<STARTUPINFOW>() as u32;
            let mut pi: PROCESS_INFORMATION = unsafe { std::mem::zeroed() };
            let ok = unsafe {
                CreateProcessW(exe_w.as_ptr(), cmdline.as_mut_ptr(), std::ptr::null(), std::ptr::null(), 0,
                               CREATE_SUSPENDED, std::ptr::null(), dir_w.as_ptr(), &si, &mut pi)
            };
            if ok == 0 { return Err(format!("could not start gta_sa.exe (error {}); try running as administrator", unsafe { GetLastError() })) }
            Ok(Game { pi })
        }

        /// LoadLibraryW(path) inside the game; Ok when the DLL loaded
        pub fn inject(&self, dll: &std::path::Path) -> Result<(), String> {
            let h: HANDLE = self.pi.hProcess;
            let path = wide(dll.as_os_str());
            let bytes = path.len() * 2;
            unsafe {
                let k32 = GetModuleHandleW(wide(std::ffi::OsStr::new("kernel32.dll")).as_ptr());
                let ll = GetProcAddress(k32, b"LoadLibraryW\0".as_ptr()).ok_or("LoadLibraryW not found")?;
                let mem = VirtualAllocEx(h, std::ptr::null(), bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if mem.is_null() { return Err(format!("VirtualAllocEx failed ({})", GetLastError())) }
                let mut written = 0usize;
                if WriteProcessMemory(h, mem, path.as_ptr() as _, bytes, &mut written) == 0 {
                    VirtualFreeEx(h, mem, 0, MEM_RELEASE);
                    return Err(format!("WriteProcessMemory failed ({})", GetLastError()));
                }
                let th = CreateRemoteThread(h, std::ptr::null(), 0, Some(std::mem::transmute(ll)), mem, 0, std::ptr::null_mut());
                if th.is_null() { VirtualFreeEx(h, mem, 0, MEM_RELEASE); return Err(format!("CreateRemoteThread failed ({})", GetLastError())) }
                let waited = WaitForSingleObject(th, 15000);
                let mut code = 0u32;
                GetExitCodeThread(th, &mut code);
                CloseHandle(th);
                VirtualFreeEx(h, mem, 0, MEM_RELEASE);
                if waited != WAIT_OBJECT_0 { return Err("LoadLibrary timed out".into()) }
                if code == 0 { return Err("LoadLibrary returned NULL (missing dependency or wrong architecture)".into()) }
            }
            Ok(())
        }

        pub fn resume(&self) { unsafe { ResumeThread(self.pi.hThread); } }
        pub fn kill(&self) { unsafe { TerminateProcess(self.pi.hProcess, 1); } }
    }

    impl Drop for Game {
        fn drop(&mut self) { unsafe { CloseHandle(self.pi.hThread); CloseHandle(self.pi.hProcess); } }
    }
}

#[cfg(windows)]
fn launch(ip: &str, port: u16, name: &str, password: &str, gta_dir: &str) -> Result<String, String> {
    if cfg!(target_pointer_width = "64") {
        return Err("This is a 64-bit build; gta_sa.exe is 32-bit. Use the 32-bit MP Launcher (i686) to join.".into());
    }
    let d = std::path::Path::new(gta_dir);
    let exe = d.join("gta_sa.exe");
    let samp = d.join("samp.dll");
    if !exe.is_file() { return Err(format!("gta_sa.exe not found in {gta_dir}")); }
    if !samp.is_file() { return Err("samp.dll not found: install SA-MP 0.3.7 into the GTA San Andreas folder".into()); }
    let q = |s: &str| if s.contains(' ') { format!("\"{s}\"") } else { s.to_string() };
    let mut args = format!("-c -n {} -h {} -p {}", q(name), ip, port);
    if !password.is_empty() { args.push_str(&format!(" -z {}", q(password))); }
    let game = win::Game::start(&exe, d, &args)?;
    if let Err(e) = game.inject(&samp) { game.kill(); return Err(format!("samp.dll: {e}")); }
    let mut msg = format!("Joining {ip}:{port} as {name} (SA-MP)");
    if let Some(o) = omp_dll(d) {
        match game.inject(&o) {
            Ok(()) => msg = format!("Joining {ip}:{port} as {name} (SA-MP + open.mp client)"),
            Err(e) => msg.push_str(&format!(" - open.mp client DLL not loaded: {e}")),
        }
    }
    game.resume();
    Ok(msg)
}

#[cfg(not(windows))]
fn launch(ip: &str, port: u16, _name: &str, _password: &str, _gta_dir: &str) -> Result<String, String> {
    Err(format!("Joining needs Windows (GTA San Andreas + SA-MP). Server: {ip}:{port}"))
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![query, servers, featured, samp_settings, check_game, play])
        .run(tauri::generate_context!())
        .expect("MP Launcher failed to start");
}
