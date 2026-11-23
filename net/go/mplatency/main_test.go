package main

import (
	"context"
	"encoding/json"
	"math"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestParseWires(t *testing.T) {
	in := `# comment
asia-mqtt   | region | mqtt | mpsamp.duckdns.org:1883 | Asia broker (MQTT) | user=mp
lob         | lobby  | lobby | - | lobby only
tpl         | oracle | tcp  | {ORACLE_IP}:7777 | template
omp         | omp    | samp | 127.0.0.1:7777 | open.mp
`
	ts := ParseWires(strings.NewReader(in))
	if len(ts) != 2 || ts[0].ID != "asia-mqtt" || ts[0].Port != "1883" || ts[1].Kind != "samp" {
		t.Fatalf("got %+v", ts)
	}
}

func TestParseTarget(t *testing.T) {
	x, err := ParseTarget("Crossplay=samp://1.2.3.4:7777")
	if err != nil || x.Name != "Crossplay" || x.Kind != "samp" || x.Host != "1.2.3.4" || x.Port != "7777" {
		t.Fatalf("%+v %v", x, err)
	}
	if _, err := ParseTarget("nohost"); err == nil {
		t.Fatal("want error")
	}
}

func TestStats(t *testing.T) {
	s := NewStats(4)
	s.Add(10, true, "")
	s.Add(20, true, "")
	s.Add(0, false, "timeout")
	if !s.Add(30, true, "") {
		t.Fatal("down -> up must report a change")
	}
	sum := s.Summary()
	if sum.Samples != 4 || sum.AvgMs != 20 || sum.LossPct != 25 || sum.MinMs != 10 || sum.MaxMs != 30 {
		t.Fatalf("%+v", sum)
	}
	s.Add(40, true, "") // wraps: 20, lost, 30, 40
	sum = s.Summary()
	if sum.LastMs != 40 || math.Abs(sum.AvgMs-30) > 1e-9 {
		t.Fatalf("%+v", sum)
	}
}

// A local SA-MP 'p' answer and a TCP listener: the probes measure real round trips.
func TestProbes(t *testing.T) {
	pc, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Skip(err)
	}
	defer pc.Close()
	go func() {
		b := make([]byte, 512)
		for {
			n, a, err := pc.ReadFrom(b)
			if err != nil {
				return
			}
			if n >= 11 && string(b[:4]) == "SAMP" {
				_, _ = pc.WriteTo(b[:n], a) // 'p' is answered with the same packet
			} else if n >= 1 && b[0] == 14 {
				_, _ = pc.WriteTo([]byte{15, 1, 2, 3}, a)
			} else {
				_, _ = pc.WriteTo(b[:n], a)
			}
		}
	}()
	_, port, _ := net.SplitHostPort(pc.LocalAddr().String())
	for _, k := range []string{"samp", "mp", "udp"} {
		if _, err := Probe(context.Background(), Target{Kind: k, Host: "127.0.0.1", Port: port}, time.Second); err != nil {
			t.Fatalf("%s: %v", k, err)
		}
	}
	ln, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Skip(err)
	}
	defer ln.Close()
	_, tport, _ := net.SplitHostPort(ln.Addr().String())
	if _, err := Probe(context.Background(), Target{Kind: "tcp", Host: "127.0.0.1", Port: tport}, time.Second); err != nil {
		t.Fatal(err)
	}
	ln.Close()
	if _, err := Probe(context.Background(), Target{Kind: "tcp", Host: "127.0.0.1", Port: tport}, 300*time.Millisecond); err == nil {
		t.Fatal("closed port must fail")
	}
}

func TestWriteJSON(t *testing.T) {
	down := Target{ID: "x", Group: "vps", Kind: "tcp", Host: "127.0.0.1", Port: "1", Name: "down"}
	m := &Monitor{targets: []Target{down}, timeout: 200 * time.Millisecond, workers: 2, geo: NewGeoCache(""), stats: map[string]*Stats{"x": NewStats(8)}}
	m.Round(context.Background())
	p := filepath.Join(t.TempDir(), "s.json")
	if err := m.WriteJSON(p); err != nil {
		t.Fatal(err)
	}
	b, _ := os.ReadFile(p)
	var doc struct {
		Round   int   `json:"round"`
		Targets []Row `json:"targets"`
	}
	if err := json.Unmarshal(b, &doc); err != nil || doc.Round != 1 || len(doc.Targets) != 1 || doc.Targets[0].Status != "OFFLINE" || doc.Targets[0].Stats.LastMs != -1 {
		t.Fatalf("bad json %v: %s", err, b)
	}
}
