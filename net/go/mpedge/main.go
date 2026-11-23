// mpedge: MP NetWire edge on gnet (github.com/JebetrexxMC/gnet fork of panjf2000/gnet).
//
// One UDP port that sits in front of an open.mp / SA-MP (or MP) server:
//   - "MPNETPNG" pings are echoed straight away (the same ping mpnetd and the game's NetWire use),
//     so the edge is a normal "udp" wire in net/wires.txt;
//   - SA-MP queries ('i' info, 'r' rules, 'c' clients, 'd' details) are answered from a cache that
//     is refreshed from the backend every few seconds, and 'p' is answered locally.
//     Server lists and launchers that hammer the query port then never reach the game server and
//     every player still gets a fast, steady ping.
//   - MP PKT_QUERY (type 14) gets the cached PKT_INFO of an MP backend when -mp is set.
//
// It does not relay game traffic: players join the backend address shown in the server list.
//
//	go run ./mpedge -listen :7777 -backend 127.0.0.1:7778
package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"log"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"github.com/panjf2000/gnet/v2"
)

type cache struct {
	mu   sync.RWMutex
	resp map[byte][]byte // opcode -> backend answer (without the 11-byte header)
	mp   []byte          // MP PKT_INFO
	at   time.Time
}

type edge struct {
	gnet.BuiltinEventEngine
	backend *net.UDPAddr
	mpMode  bool
	c       cache
	hits    atomic.Uint64
	pings   atomic.Uint64
}

func sampHeader(dst []byte, a *net.UDPAddr, op byte) []byte {
	dst = append(dst, 'S', 'A', 'M', 'P')
	ip := a.IP.To4()
	if ip == nil {
		ip = net.IPv4zero.To4()
	}
	dst = append(dst, ip...)
	dst = binary.LittleEndian.AppendUint16(dst, uint16(a.Port))
	return append(dst, op)
}

// refresh asks the backend for every cached opcode once.
func (e *edge) refresh() {
	conn, err := net.DialUDP("udp4", nil, e.backend)
	if err != nil {
		log.Printf("backend: %v", err)
		return
	}
	defer conn.Close()
	buf := make([]byte, 65536)
	fresh := map[byte][]byte{}
	if e.mpMode {
		q := []byte{14, 0, 0, 0, 0}
		binary.LittleEndian.PutUint32(q[1:], uint32(time.Now().UnixNano()))
		_ = conn.SetDeadline(time.Now().Add(1500 * time.Millisecond))
		if _, err := conn.Write(q); err == nil {
			if n, err := conn.Read(buf); err == nil && n > 0 && buf[0] == 15 {
				e.c.mu.Lock()
				e.c.mp = append([]byte(nil), buf[:n]...)
				e.c.at = time.Now()
				e.c.mu.Unlock()
			}
		}
		return
	}
	for _, op := range []byte{'i', 'r', 'c', 'd'} {
		q := sampHeader(nil, e.backend, op)
		_ = conn.SetDeadline(time.Now().Add(1500 * time.Millisecond))
		if _, err := conn.Write(q); err != nil {
			continue
		}
		n, err := conn.Read(buf)
		if err != nil || n < 11 || !bytes.Equal(buf[:4], []byte("SAMP")) || buf[10] != op {
			continue
		}
		fresh[op] = append([]byte(nil), buf[11:n]...)
	}
	if len(fresh) > 0 {
		e.c.mu.Lock()
		e.c.resp = fresh
		e.c.at = time.Now()
		e.c.mu.Unlock()
	}
}

func (e *edge) OnTraffic(c gnet.Conn) gnet.Action {
	pkt, _ := c.Next(-1)
	n := len(pkt)
	switch {
	case n == 16 && bytes.Equal(pkt[:8], []byte("MPNETPNG")):
		e.pings.Add(1)
		_, _ = c.Write(pkt)
	case n >= 11 && bytes.Equal(pkt[:4], []byte("SAMP")):
		op := pkt[10]
		if op == 'p' && n >= 15 {
			e.pings.Add(1)
			_, _ = c.Write(pkt[:15])
			return gnet.None
		}
		e.c.mu.RLock()
		body, ok := e.c.resp[op]
		e.c.mu.RUnlock()
		if ok {
			e.hits.Add(1)
			out := make([]byte, 0, 11+len(body))
			out = append(out, pkt[:11]...) // echo the asker's header (ip/port it used)
			_, _ = c.Write(append(out, body...))
		}
	case n == 5 && pkt[0] == 14 && e.mpMode:
		e.c.mu.RLock()
		info := e.c.mp
		e.c.mu.RUnlock()
		if len(info) >= 5 {
			e.hits.Add(1)
			out := append([]byte(nil), info...)
			copy(out[1:5], pkt[1:5]) // the client matches its own time field
			_, _ = c.Write(out)
		}
	}
	return gnet.None
}

func main() {
	listen := flag.String("listen", ":7777", "UDP address to answer on")
	backend := flag.String("backend", "127.0.0.1:7778", "game server (open.mp / SA-MP, or MP with -mp)")
	mp := flag.Bool("mp", false, "backend is an MP server (PKT_QUERY / PKT_INFO)")
	every := flag.Duration("every", 3*time.Second, "cache refresh interval")
	flag.Parse()

	b, err := net.ResolveUDPAddr("udp4", *backend)
	if err != nil {
		log.Fatal(err)
	}
	e := &edge{backend: b, mpMode: *mp}
	e.refresh()
	go func() {
		t := time.NewTicker(*every)
		s := time.NewTicker(time.Minute)
		for {
			select {
			case <-t.C:
				e.refresh()
			case <-s.C:
				e.c.mu.RLock()
				age := time.Since(e.c.at).Round(time.Second)
				e.c.mu.RUnlock()
				log.Printf("pings %d, cached answers %d, cache age %s", e.pings.Load(), e.hits.Load(), age)
			}
		}
	}()
	log.Printf("mpedge on udp %s -> %s (mp=%v)", *listen, b, *mp)
	log.Fatal(gnet.Run(e, "udp4://"+*listen, gnet.WithMulticore(true), gnet.WithReusePort(true)))
}
