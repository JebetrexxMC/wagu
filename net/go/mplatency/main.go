// mplatency: MP NetWire 24/7 server latency + location monitor (Go, standard library only).
//
// Built on the "Server Latency & Location Monitor" idea (TCP connect time, goroutine per server,
// geolocation cached once) and fixed / extended for MP:
//
//   - the ip-api URL was "http://ip-api.com" + ip (no "/json/"): every lookup failed. Lookups now
//     go through ip-api's batch endpoint (100 IPs per request, once at start + every 6 h), so the
//     24/7 loop never hits the 45 requests/minute limit;
//
//   - the AWS hosts were "://amazonaws.com" (not a host name): now real EC2 endpoints;
//
//   - game servers are UDP: TCP to 7777 always said OFFLINE. "samp" wires use the SA-MP query 'p'
//     (open.mp / SA-MP), "mp" wires the MP PKT_QUERY (the in-game server list), "udp" the MPNETPNG
//     echo of mpnetd / mpedge. tcp / tls / http / mqtt wires use the TCP handshake time;
//
//   - reads net/wires.txt (the same list the NetWire workflow probes), -t adds more targets;
//
//   - keeps per-target stats (last, avg, min, max, jitter, loss, uptime) over a sliding window;
//
//   - HTTP: / (text table), /status.json, /metrics (Prometheus), /healthz;
//
//   - optional Discord webhook / Telegram alert when a target goes down or comes back;
//
//   - stops cleanly on SIGINT / SIGTERM (systemd, nohup, Termux).
//
//     go run ./mplatency -wires ../wires.txt -every 5s -http :8099
package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"math"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"sort"
	"strings"
	"sync"
	"syscall"
	"text/tabwriter"
	"time"
)

// Target is one server / wire to watch.
type Target struct {
	ID    string `json:"id"`
	Group string `json:"group"`
	Kind  string `json:"kind"` // tcp tls http mqtt samp mp udp
	Host  string `json:"host"`
	Port  string `json:"port"`
	Name  string `json:"name"`
}

func (t Target) Addr() string { return net.JoinHostPort(t.Host, t.Port) }

// built-in targets (the original example, with real host names) - used when no wires file is given
var defaultTargets = []Target{
	{ID: "cf-dns", Group: "web", Kind: "tcp", Host: "1.1.1.1", Port: "443", Name: "Cloudflare DNS"},
	{ID: "g-dns", Group: "web", Kind: "tcp", Host: "8.8.8.8", Port: "443", Name: "Google DNS"},
	{ID: "aws-sg", Group: "web", Kind: "tcp", Host: "ec2.ap-southeast-1.amazonaws.com", Port: "443", Name: "AWS Singapore"},
	{ID: "aws-tyo", Group: "web", Kind: "tcp", Host: "ec2.ap-northeast-1.amazonaws.com", Port: "443", Name: "AWS Tokyo"},
	{ID: "aws-use", Group: "web", Kind: "tcp", Host: "ec2.us-east-1.amazonaws.com", Port: "443", Name: "AWS US-East"},
	{ID: "asia-mqtt", Group: "region", Kind: "tcp", Host: "mpsamp.duckdns.org", Port: "1883", Name: "MP Asia broker"},
	{ID: "eu-mqtt", Group: "region", Kind: "tcp", Host: "eusamp.duckdns.org", Port: "1883", Name: "MP Europe broker"},
	{ID: "na-mqtt", Group: "region", Kind: "tcp", Host: "nasamp.duckdns.org", Port: "1883", Name: "MP NA broker"},
	{ID: "oc-mqtt", Group: "region", Kind: "tcp", Host: "ocsamp.duckdns.org", Port: "1883", Name: "MP Oceania broker"},
}

// ParseWires reads net/wires.txt: id | group | kind | host:port | name | options
func ParseWires(r io.Reader) []Target {
	var out []Target
	sc := bufio.NewScanner(r)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || line[0] == '#' {
			continue
		}
		f := strings.Split(line, "|")
		if len(f) < 4 {
			continue
		}
		for i := range f {
			f[i] = strings.TrimSpace(f[i])
		}
		kind := strings.ToLower(f[2])
		if kind == "lobby" || strings.Contains(f[3], "{") { // lobby-only servers / templates have no address
			continue
		}
		host, port, err := net.SplitHostPort(f[3])
		if err != nil || host == "" || port == "" {
			continue
		}
		name := f[0]
		if len(f) > 4 && f[4] != "" {
			name = f[4]
		}
		out = append(out, Target{ID: f[0], Group: f[1], Kind: kind, Host: host, Port: port, Name: name})
	}
	return out
}

// ParseTarget: "name=host:port" or "kind://host:port" or "host:port" (-t flag)
func ParseTarget(s string) (Target, error) {
	t := Target{Kind: "tcp", Group: "custom"}
	if i := strings.Index(s, "="); i > 0 {
		t.Name, s = s[:i], s[i+1:]
	}
	if i := strings.Index(s, "://"); i > 0 {
		t.Kind, s = strings.ToLower(s[:i]), s[i+3:]
	}
	h, p, err := net.SplitHostPort(s)
	if err != nil {
		return t, fmt.Errorf("%q: want host:port (%v)", s, err)
	}
	t.Host, t.Port = h, p
	if t.Name == "" {
		t.Name = s
	}
	t.ID = t.Kind + "-" + s
	return t, nil
}

// ---- probes --------------------------------------------------------------------------------

// Probe returns the round-trip time of one check.
func Probe(ctx context.Context, t Target, timeout time.Duration) (time.Duration, error) {
	switch t.Kind {
	case "samp":
		return probeSAMP(t, timeout)
	case "mp":
		return probeMP(t, timeout)
	case "udp":
		return probeEcho(t, timeout)
	default: // tcp tls http mqtt: the TCP handshake is the network latency
		d := net.Dialer{Timeout: timeout}
		start := time.Now()
		c, err := d.DialContext(ctx, "tcp", t.Addr())
		el := time.Since(start)
		if err != nil {
			return 0, err
		}
		c.Close()
		return el, nil
	}
}

func udpRoundTrip(t Target, timeout time.Duration, q []byte, ok func([]byte) bool) (time.Duration, error) {
	ra, err := net.ResolveUDPAddr("udp4", t.Addr())
	if err != nil {
		return 0, err
	}
	c, err := net.DialUDP("udp4", nil, ra)
	if err != nil {
		return 0, err
	}
	defer c.Close()
	buf := make([]byte, 2048)
	deadline := time.Now().Add(timeout)
	_ = c.SetDeadline(deadline)
	start := time.Now()
	if _, err := c.Write(q); err != nil {
		return 0, err
	}
	for {
		n, err := c.Read(buf)
		if err != nil {
			return 0, err
		}
		if ok(buf[:n]) {
			return time.Since(start), nil
		}
	}
}

// SAMPQuery builds a SA-MP query packet: "SAMP" + ip(4) + port(2, LE) + opcode [+ payload]
func SAMPQuery(ip net.IP, port int, op byte, payload []byte) []byte {
	q := []byte("SAMP")
	ip4 := ip.To4()
	if ip4 == nil {
		ip4 = net.IPv4zero.To4()
	}
	q = append(q, ip4...)
	q = binary.LittleEndian.AppendUint16(q, uint16(port))
	q = append(q, op)
	return append(q, payload...)
}

func probeSAMP(t Target, timeout time.Duration) (time.Duration, error) {
	ra, err := net.ResolveUDPAddr("udp4", t.Addr())
	if err != nil {
		return 0, err
	}
	var nonce [4]byte
	_, _ = rand.Read(nonce[:])
	q := SAMPQuery(ra.IP, ra.Port, 'p', nonce[:])
	return udpRoundTrip(t, timeout, q, func(b []byte) bool {
		return len(b) >= 15 && bytes.Equal(b[:4], []byte("SAMP")) && b[10] == 'p' && bytes.Equal(b[11:15], nonce[:])
	})
}

// MPQuery: PKT_QUERY (type 14) + uint32 time, the reply starts with PKT_INFO (15) - src/net/protocol.h
func MPQuery() []byte {
	q := []byte{14, 0, 0, 0, 0}
	binary.LittleEndian.PutUint32(q[1:], uint32(time.Now().UnixNano()))
	return q
}

func probeMP(t Target, timeout time.Duration) (time.Duration, error) {
	return udpRoundTrip(t, timeout, MPQuery(), func(b []byte) bool { return len(b) > 0 && b[0] == 15 })
}

func probeEcho(t Target, timeout time.Duration) (time.Duration, error) {
	q := []byte("MPNETPNG")
	var tag [8]byte
	_, _ = rand.Read(tag[:])
	q = append(q, tag[:]...)
	return udpRoundTrip(t, timeout, q, func(b []byte) bool { return bytes.Contains(b, tag[:]) })
}

// ---- geolocation (ip-api.com batch, cached) -----------------------------------------------------

type Geo struct {
	Status      string `json:"status"`
	Country     string `json:"country"`
	CountryCode string `json:"countryCode"`
	City        string `json:"city"`
	ISP         string `json:"isp"`
	Query       string `json:"query"`
}

func (g Geo) String() string {
	if g.Status != "success" {
		return g.Query
	}
	return fmt.Sprintf("%s, %s (%s)", g.City, g.Country, g.ISP)
}

type GeoCache struct {
	mu   sync.RWMutex
	ip   map[string]string // host -> first IPv4
	geo  map[string]Geo    // ip -> location
	api  string
	http *http.Client
}

func NewGeoCache(api string) *GeoCache {
	return &GeoCache{ip: map[string]string{}, geo: map[string]Geo{}, api: api, http: &http.Client{Timeout: 8 * time.Second}}
}

// Refresh resolves every host and looks the IPs up, 100 per request (ip-api's batch limit).
func (g *GeoCache) Refresh(targets []Target) {
	if g.api == "" {
		return
	}
	ips := map[string]bool{}
	resolved := map[string]string{}
	for _, t := range targets {
		if _, ok := resolved[t.Host]; ok {
			continue
		}
		addrs, err := net.LookupIP(t.Host)
		if err != nil {
			continue
		}
		for _, a := range addrs {
			if a4 := a.To4(); a4 != nil {
				resolved[t.Host] = a4.String()
				ips[a4.String()] = true
				break
			}
		}
	}
	list := make([]string, 0, len(ips))
	for ip := range ips {
		list = append(list, ip)
	}
	sort.Strings(list)
	got := map[string]Geo{}
	for i := 0; i < len(list); i += 100 {
		j := i + 100
		if j > len(list) {
			j = len(list)
		}
		body, _ := json.Marshal(list[i:j])
		resp, err := g.http.Post(g.api+"?fields=status,country,countryCode,city,isp,query", "application/json", bytes.NewReader(body))
		if err != nil {
			log.Printf("geo: %v", err)
			break
		}
		var res []Geo
		err = json.NewDecoder(resp.Body).Decode(&res)
		resp.Body.Close()
		if err != nil {
			log.Printf("geo: %v", err)
			break
		}
		for _, r := range res {
			got[r.Query] = r
		}
	}
	g.mu.Lock()
	for h, ip := range resolved {
		g.ip[h] = ip
	}
	for ip, r := range got {
		g.geo[ip] = r
	}
	g.mu.Unlock()
}

func (g *GeoCache) Lookup(host string) (ip string, geo Geo) {
	g.mu.RLock()
	defer g.mu.RUnlock()
	ip = g.ip[host]
	geo, ok := g.geo[ip]
	if !ok {
		geo = Geo{Query: ip}
		if ip == "" {
			geo.Query = "unknown location"
		}
	}
	return ip, geo
}

// ---- stats ---------------------------------------------------------------------------------------

type Stats struct {
	samples []float64 // ms, NaN = lost
	next    int
	full    bool
	total   uint64
	up      uint64
	Online  bool
	LastErr string
	Since   time.Time // when Online last changed
}

func NewStats(window int) *Stats { return &Stats{samples: make([]float64, window)} }

func (s *Stats) Add(ms float64, ok bool, err string) (changed bool) {
	if !ok {
		ms = math.NaN()
	}
	s.samples[s.next] = ms
	s.next = (s.next + 1) % len(s.samples)
	if s.next == 0 {
		s.full = true
	}
	s.total++
	if ok {
		s.up++
	}
	s.LastErr = err
	if s.total == 1 || ok != s.Online {
		changed = s.total > 1
		s.Online = ok
		s.Since = time.Now()
	}
	return changed
}

type Summary struct {
	LastMs   float64 `json:"last_ms"`
	AvgMs    float64 `json:"avg_ms"`
	MinMs    float64 `json:"min_ms"`
	MaxMs    float64 `json:"max_ms"`
	JitterMs float64 `json:"jitter_ms"`
	LossPct  float64 `json:"loss_pct"`
	UptimePc float64 `json:"uptime_pct"`
	Samples  int     `json:"samples"`
}

func (s *Stats) Summary() Summary {
	n := s.next
	if s.full {
		n = len(s.samples)
	}
	var out Summary
	out.Samples = n
	if n == 0 {
		return out
	}
	order := make([]float64, 0, n)
	start := 0
	if s.full {
		start = s.next
	}
	for i := 0; i < n; i++ {
		order = append(order, s.samples[(start+i)%len(s.samples)])
	}
	out.LastMs = order[len(order)-1]
	lost, cnt, sum, jit, jn := 0, 0, 0.0, 0.0, 0
	out.MinMs, out.MaxMs = math.Inf(1), math.Inf(-1)
	prev := math.NaN()
	for _, v := range order {
		if math.IsNaN(v) {
			lost++
			continue
		}
		cnt++
		sum += v
		out.MinMs = math.Min(out.MinMs, v)
		out.MaxMs = math.Max(out.MaxMs, v)
		if !math.IsNaN(prev) {
			jit += math.Abs(v - prev)
			jn++
		}
		prev = v
	}
	if cnt > 0 {
		out.AvgMs = sum / float64(cnt)
	} else {
		out.MinMs, out.MaxMs = 0, 0
	}
	if jn > 0 {
		out.JitterMs = jit / float64(jn)
	}
	out.LossPct = 100 * float64(lost) / float64(n)
	if s.total > 0 {
		out.UptimePc = 100 * float64(s.up) / float64(s.total)
	}
	return out
}

// Grade: the same colours the NetWire status page uses
func Grade(online bool, s Summary) string {
	switch {
	case !online:
		return "down"
	case s.LastMs < 120 && s.LossPct < 5:
		return "green"
	case s.LastMs < 250 && s.LossPct < 20:
		return "yellow"
	default:
		return "red"
	}
}

// ---- monitor ---------------------------------------------------------------------------------------

type Monitor struct {
	targets []Target
	timeout time.Duration
	workers int
	geo     *GeoCache
	alert   *Alerter

	mu    sync.RWMutex
	stats map[string]*Stats
	round uint64
	last  time.Time
}

type Row struct {
	Target
	Status   string  `json:"status"`
	Grade    string  `json:"grade"`
	IP       string  `json:"ip"`
	Location string  `json:"location"`
	Country  string  `json:"country_code"`
	Error    string  `json:"error,omitempty"`
	Since    string  `json:"since"`
	Stats    Summary `json:"stats"`
}

func (m *Monitor) Round(ctx context.Context) {
	type res struct {
		t   Target
		ms  float64
		ok  bool
		err string
	}
	out := make(chan res, len(m.targets))
	sem := make(chan struct{}, m.workers)
	var wg sync.WaitGroup
	for _, t := range m.targets {
		wg.Add(1)
		go func(t Target) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			el, err := Probe(ctx, t, m.timeout)
			r := res{t: t, ok: err == nil, ms: float64(el.Microseconds()) / 1000.0}
			if err != nil {
				r.err = err.Error()
			}
			out <- r
		}(t)
	}
	wg.Wait()
	close(out)
	m.mu.Lock()
	var changes []Row
	for r := range out {
		s := m.stats[r.t.ID]
		if s.Add(r.ms, r.ok, r.err) {
			changes = append(changes, m.rowLocked(r.t))
		}
	}
	m.round++
	m.last = time.Now()
	m.mu.Unlock()
	if m.alert != nil {
		for _, c := range changes {
			m.alert.Send(c)
		}
	}
}

func (m *Monitor) rowLocked(t Target) Row {
	s := m.stats[t.ID]
	sum := s.Summary()
	ip, g := m.geo.Lookup(t.Host)
	r := Row{Target: t, IP: ip, Location: g.String(), Country: g.CountryCode, Error: s.LastErr, Stats: sum}
	r.Status = "OFFLINE"
	if s.Online {
		r.Status = "ONLINE"
	}
	r.Grade = Grade(s.Online, sum)
	if !s.Since.IsZero() {
		r.Since = s.Since.UTC().Format(time.RFC3339)
	}
	if math.IsNaN(r.Stats.LastMs) {
		r.Stats.LastMs = -1
	}
	return r
}

func (m *Monitor) Rows() []Row {
	m.mu.RLock()
	defer m.mu.RUnlock()
	rows := make([]Row, 0, len(m.targets))
	for _, t := range m.targets {
		rows = append(rows, m.rowLocked(t))
	}
	sort.SliceStable(rows, func(i, j int) bool {
		if rows[i].Group != rows[j].Group {
			return rows[i].Group < rows[j].Group
		}
		return rows[i].Name < rows[j].Name
	})
	return rows
}

// Status is the /status.json document (also written by -json).
func (m *Monitor) Status() map[string]any {
	m.mu.RLock()
	round, last := m.round, m.last
	m.mu.RUnlock()
	return map[string]any{"round": round, "time": last.UTC().Format(time.RFC3339), "targets": m.Rows()}
}

// WriteJSON writes Status to path (temp file + rename, so readers never see half a file).
func (m *Monitor) WriteJSON(path string) error {
	if path == "" {
		return nil
	}
	b, err := json.MarshalIndent(m.Status(), "", " ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func fmtMs(v float64) string {
	if v < 0 || math.IsNaN(v) {
		return "N/A"
	}
	return fmt.Sprintf("%.2f ms", v)
}

func (m *Monitor) WriteTable(w io.Writer) {
	m.mu.RLock()
	round, last := m.round, m.last
	m.mu.RUnlock()
	fmt.Fprintf(w, "--- MP NetWire latency [%s] round %d, %d targets ---\n", last.Format("2006-01-02 15:04:05"), round, len(m.targets))
	tw := tabwriter.NewWriter(w, 0, 0, 2, ' ', 0)
	fmt.Fprintln(tw, "STATUS\tGRADE\tNAME\tKIND\tLAST\tAVG\tJITTER\tLOSS\tUPTIME\tLOCATION")
	for _, r := range m.Rows() {
		fmt.Fprintf(tw, "[%s]\t%s\t%s\t%s\t%s\t%s\t%.1f\t%.0f%%\t%.1f%%\t%s\n", r.Status, r.Grade, r.Name, r.Kind,
			fmtMs(r.Stats.LastMs), fmtMs(r.Stats.AvgMs), r.Stats.JitterMs, r.Stats.LossPct, r.Stats.UptimePc, r.Location)
	}
	tw.Flush()
}

func (m *Monitor) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		m.WriteTable(w)
	})
	mux.HandleFunc("/status.json", func(w http.ResponseWriter, r *http.Request) {
		m.mu.RLock()
		round, last := m.round, m.last
		m.mu.RUnlock()
		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Access-Control-Allow-Origin", "*")
		_ = json.NewEncoder(w).Encode(map[string]any{"round": round, "time": last.UTC().Format(time.RFC3339), "targets": m.Rows()})
	})
	mux.HandleFunc("/metrics", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/plain; version=0.0.4")
		fmt.Fprintln(w, "# TYPE mp_latency_ms gauge\n# TYPE mp_up gauge\n# TYPE mp_loss_pct gauge")
		for _, row := range m.Rows() {
			l := fmt.Sprintf("id=%q,group=%q,kind=%q,country=%q", row.ID, row.Group, row.Kind, row.Country)
			up := 0
			if row.Status == "ONLINE" {
				up = 1
			}
			fmt.Fprintf(w, "mp_up{%s} %d\nmp_latency_ms{%s} %g\nmp_loss_pct{%s} %g\n", l, up, l, row.Stats.LastMs, l, row.Stats.LossPct)
		}
	})
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) { _, _ = io.WriteString(w, "ok\n") })
	return mux
}

// ---- alerts ------------------------------------------------------------------------------------------

type Alerter struct {
	discord, tgToken, tgChat string
	http                     *http.Client
}

func (a *Alerter) Send(r Row) {
	msg := fmt.Sprintf("MP NetWire: %s (%s %s) is %s", r.Name, r.Kind, r.Addr(), r.Status)
	if r.Status == "ONLINE" {
		msg += fmt.Sprintf(" again, %s", fmtMs(r.Stats.LastMs))
	} else if r.Error != "" {
		msg += " - " + r.Error
	}
	log.Print(msg)
	if a.discord != "" {
		b, _ := json.Marshal(map[string]string{"content": msg})
		if resp, err := a.http.Post(a.discord, "application/json", bytes.NewReader(b)); err == nil {
			resp.Body.Close()
		}
	}
	if a.tgToken != "" && a.tgChat != "" {
		u := "https://api.telegram.org/bot" + a.tgToken + "/sendMessage"
		if resp, err := a.http.PostForm(u, url.Values{"chat_id": {a.tgChat}, "text": {msg}}); err == nil {
			resp.Body.Close()
		}
	}
}

// ---- main ----------------------------------------------------------------------------------------------

type multiFlag []string

func (f *multiFlag) String() string     { return strings.Join(*f, ",") }
func (f *multiFlag) Set(v string) error { *f = append(*f, v); return nil }

func main() {
	var extra multiFlag
	wires := flag.String("wires", "", "net/wires.txt to watch (empty = built-in list)")
	groups := flag.String("groups", "", "only these wire groups, comma separated (e.g. region,vps,omp)")
	every := flag.Duration("every", 5*time.Second, "time between rounds")
	timeout := flag.Duration("timeout", 2*time.Second, "timeout of one check")
	window := flag.Int("window", 120, "samples kept per target (avg / jitter / loss)")
	workers := flag.Int("workers", 64, "checks running at the same time")
	httpAddr := flag.String("http", "", "status page address, e.g. :8099 (/, /status.json, /metrics)")
	geoAPI := flag.String("geo", "http://ip-api.com/batch", "ip-api batch URL (empty = no locations)")
	quiet := flag.Bool("quiet", false, "do not print the table every round")
	once := flag.Bool("once", false, "one round, print, exit (exit code 1 if a target is down)")
	discord := flag.String("discord", os.Getenv("MP_DISCORD_WEBHOOK"), "Discord webhook for down / up alerts")
	tgToken := flag.String("telegram-token", os.Getenv("MP_TELEGRAM_TOKEN"), "Telegram bot token for alerts")
	tgChat := flag.String("telegram-chat", os.Getenv("MP_TELEGRAM_CHAT"), "Telegram chat id for alerts")
	rounds := flag.Int("rounds", 1, "with -once: rounds to run (one every -every) before printing")
	jsonOut := flag.String("json", "", "write /status.json to this file (after -once, or every round)")
	flag.Var(&extra, "t", "extra target: [name=][kind://]host:port (repeat)")
	flag.Parse()

	targets := defaultTargets
	if *wires != "" {
		f, err := os.Open(*wires)
		if err != nil {
			log.Fatalf("wires: %v", err)
		}
		targets = ParseWires(f)
		f.Close()
	}
	if *groups != "" {
		keep := map[string]bool{}
		for _, g := range strings.Split(*groups, ",") {
			keep[strings.TrimSpace(g)] = true
		}
		var sel []Target
		for _, t := range targets {
			if keep[t.Group] {
				sel = append(sel, t)
			}
		}
		targets = sel
	}
	for _, s := range extra {
		t, err := ParseTarget(s)
		if err != nil {
			log.Fatal(err)
		}
		targets = append(targets, t)
	}
	if len(targets) == 0 {
		log.Fatal("no targets")
	}

	m := &Monitor{targets: targets, timeout: *timeout, workers: *workers, geo: NewGeoCache(*geoAPI), stats: map[string]*Stats{}}
	for _, t := range targets {
		m.stats[t.ID] = NewStats(*window)
	}
	if *discord != "" || (*tgToken != "" && *tgChat != "") {
		m.alert = &Alerter{discord: *discord, tgToken: *tgToken, tgChat: *tgChat, http: &http.Client{Timeout: 8 * time.Second}}
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	log.Printf("mplatency: %d targets, every %s - resolving locations...", len(targets), *every)
	m.geo.Refresh(targets)

	if *once {
		for i := 0; i < *rounds && ctx.Err() == nil; i++ {
			if i > 0 {
				select {
				case <-ctx.Done():
				case <-time.After(*every):
				}
			}
			m.Round(ctx)
		}
		m.WriteTable(os.Stdout)
		if err := m.WriteJSON(*jsonOut); err != nil {
			log.Printf("json: %v", err)
		}
		for _, r := range m.Rows() {
			if r.Status != "ONLINE" {
				os.Exit(1)
			}
		}
		return
	}

	if *httpAddr != "" {
		srv := &http.Server{Addr: *httpAddr, Handler: m.Handler(), ReadHeaderTimeout: 5 * time.Second}
		go func() {
			log.Printf("status page on http://%s/", *httpAddr)
			if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
				log.Printf("http: %v", err)
			}
		}()
		defer srv.Close()
	}

	tick := time.NewTicker(*every)
	defer tick.Stop()
	geoTick := time.NewTicker(6 * time.Hour) // IPs / hosting move rarely
	defer geoTick.Stop()
	for {
		m.Round(ctx)
		if !*quiet {
			m.WriteTable(os.Stdout)
		}
		if err := m.WriteJSON(*jsonOut); err != nil {
			log.Printf("json: %v", err)
		}
		select {
		case <-ctx.Done():
			log.Print("mplatency: stopped")
			return
		case <-geoTick.C:
			go m.geo.Refresh(targets)
		case <-tick.C:
		}
	}
}
