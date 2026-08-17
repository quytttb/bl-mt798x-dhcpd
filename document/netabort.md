# Network Boot Abort (netabort)

The `netabort` feature lets a host tool abort the U-Boot boot countdown over
the network and force the device into the blocking **web failsafe** (httpd),
without any console access. It is compatible with both the **BreedEnter**
magic packet and the **uBootEnter.py** magic packet.

Source: `uboot-mtk-20250711/net/net_abort.c`
Header: `uboot-mtk-20250711/include/net_abort.h`

---

## 1. Protocol Overview

```plain
 Host (PC)                          Device (U-Boot)
    |                                    |
    |--- UDP broadcast :37541 --------> |  magic trigger (every ~300 ms)
    |        "<PROTO>:ABORT"            |
    |                                    |  on match:
    |<--- UDP broadcast :37540 -------- |  reply "<PROTO>:ABORTED"
    |        "<PROTO>:ABORTED"          |  then enter web failsafe
    |                                    |  (httpd / DHCP / DNS)
```

The device listens on **UDP 37541** while the boot countdown is running.
When a matching magic payload arrives, it replies on **UDP 37540** with the
matching `<PROTO>:ABORTED` payload and enters the blocking web failsafe.

### 1.1 Magic payloads

| Direction | Payload       | Length (bytes) | Notes                      |
|-----------|---------------|----------------|----------------------------|
| Host -> Device | `UBOOT:ABORT`   | 11 | uBootEnter.py trigger      |
| Host -> Device | `BREED:ABORT`   | 11 | BreedEnter trigger         |
| Device -> Host | `UBOOT:ABORTED` | 13 | reply for uBootEnter.py    |
| Device -> Host | `BREED:ABORTED` | 13 | reply for BreedEnter       |

The reply mirrors the protocol of the received trigger: a `UBOOT:ABORT`
gets a `UBOOT:ABORTED`, a `BREED:ABORT` gets a `BREED:ABORTED`.

### 1.2 Ports

| Port  | Direction       | Purpose                         |
|-------|-----------------|---------------------------------|
| 37541 | Host -> Device  | trigger magic packet (broadcast)|
| 37540 | Device -> Host  | `<PROTO>:ABORTED` reply (broadcast, both src and dst port) |

### 1.3 Wire details

- The host sends the trigger as a **UDP broadcast** to `255.255.255.255:37541`.
- The host should keep **resending** the trigger (typically every 300 ms)
  until it receives the reply, because the device's listen window is short
  and the link may still be settling.
- The device replies as a **UDP broadcast** to `255.255.255.255:37540`
  (source and destination port both 37540). The host must therefore
  **bind UDP 37540** to receive the reply.
- Broadcasts require the host firewall to allow UDP on 37540/37541 on the
  interface facing the device.

---

## 2. Device-side behaviour

### 2.1 Timing / listen window

`net_abort_prepare()` initialises the ethernet device, installs the UDP
handler and then waits for the trigger. The wait covers the PHY link settle
plus the listen window:

- Default window: **1 second** (`NET_ABORT_WAIT_SEC` in `net_abort.c`).
- Overridable at runtime with the environment variable `net_abort_wait`
  (seconds). Example: `setenv net_abort_wait 3`.
- Any console key press skips the remaining wait.
- The loop polls every 10 ms and exits as soon as the magic packet is seen,
  so the actual delay is only the time until the next trigger arrives.

### 2.2 Enable / disable

The feature is compiled in via `CONFIG_MTK_NET_ABORT` and can be disabled
at runtime:

```bash
setenv disable_net_abort 1
saveenv
```

When `disable_net_abort` is set, `net_abort_prepare()` is a no-op.

### 2.3 Console command

```bash
netabort status
    Show whether listening is enabled and whether a trigger was received
    (and with which protocol: BREED or UBOOT).

netabort listen [secs]
    Manually start one listen session (uses net_abort_prepare() +
    net_abort_finish()). secs temporarily overrides net_abort_wait
    (default 1). On trigger it replies and enters the web failsafe.
```

### 2.4 On trigger

When a matching magic packet is received:

1. Broadcasts the matching `<PROTO>:ABORTED` reply to `255.255.255.255:37540`.
2. Halts the ethernet device, waits ~500 ms.
3. Enters the blocking web failsafe (`run_command("httpd")`) — HTTP :80,
   DHCP and DNS server. Typical console output:

```bash
netabort: triggered (BREED), entering web failsafe
Web failsafe UI started
URL: http://192.168.1.1/
...
```

---

## 3. Go example: dual-protocol abort tool

The tool below is a standalone host-side implementation. It sends the
abort magic packet in a loop (every 300 ms) and waits for the matching
`<PROTO>:ABORTED` reply on UDP 37540.

Select the protocol with the `-proto` flag (`breed` or `uboot`):

```bash
# Breed-style magic (BREED:ABORT / BREED:ABORTED)
go run netabort.go -proto breed

# uBootEnter-style magic (UBOOT:ABORT / UBOOT:ABORTED)
go run netabort.go -proto uboot

# Build
go build -o netabort netabort.go
```

```go
package main

import (
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"time"
)

var (
	proto   = flag.String("proto", "breed", "magic packet protocol: breed or uboot")
	port    = flag.Int("port", 37540, "local UDP port to listen for the reply")
	destIP  = flag.String("dest-ip", "255.255.255.255", "broadcast target IP")
	destPrt = flag.Int("dest-port", 37541, "target UDP port for the trigger")
	every   = flag.Duration("every", 300*time.Millisecond, "trigger resend interval")
)

func main() {
	flag.Parse()

	trigger, reply := magicPackets(*proto)

	// The device replies to broadcast:37540 (src and dst port both 37540),
	// so bind 37540 locally to receive the reply.
	conn, err := net.ListenUDP("udp", &net.UDPAddr{IP: nil, Port: *port})
	if err != nil {
		showErrorAndExit(err)
	}
	defer conn.Close()

	done := make(chan struct{})
	go sendMessageLoop(conn, done, trigger)

	recv := make([]byte, 14)
	n, _, err := conn.ReadFromUDP(recv)
	close(done)
	if err != nil {
		showErrorAndExit(err)
	}

	got := string(recv[:n])
	log.Println(got)
	if got != reply {
		log.Printf("unexpected reply, expected %q\n", reply)
		os.Exit(1)
	}
	log.Println("abort acknowledged: device should enter web failsafe now")
}

// magicPackets returns the trigger and expected reply payloads for the
// selected protocol.
func magicPackets(proto string) (trigger, reply string) {
	switch proto {
	case "breed":
		return "BREED:ABORT", "BREED:ABORTED"
	case "uboot":
		return "UBOOT:ABORT", "UBOOT:ABORTED"
	default:
		fmt.Fprintf(os.Stderr, "unknown protocol %q (use breed or uboot)\n", proto)
		os.Exit(2)
		return "", ""
	}
}

func sendMessageLoop(conn *net.UDPConn, closed <-chan struct{}, magic string) {
	tick := time.NewTicker(*every)
	defer tick.Stop()
	log.Printf("sending %q to %s:%d every %s\n", magic, *destIP, *destPrt, *every)
	for {
		select {
		case <-closed:
			log.Println("stop sending.")
			return
		case <-tick.C:
			_, _ = conn.WriteToUDP([]byte(magic), &net.UDPAddr{
				IP:   net.ParseIP(*destIP),
				Port: *destPrt,
			})
		}
	}
}

func showErrorAndExit(err error) {
	fmt.Println(err)
	fmt.Println("----------Any keys to exit---------")
	b := make([]byte, 1)
	_, _ = os.Stdin.Read(b)
	os.Exit(1)
}
```

### Expected output (breed)

```log
2026/08/14 10:00:00 sending "BREED:ABORT" to 255.255.255.255:37541 every 300ms
2026/08/14 10:00:01 BREED:ABORTED
2026/08/14 10:00:01 abort acknowledged: device should enter web failsafe now
```

---

## 4. Notes

- **Broadcast on the correct interface**: the host must send/receive on the
  network interface connected to the device (same subnet). Binding
  `0.0.0.0:37540` receives broadcasts on all interfaces.
- **Firewall**: allow inbound UDP 37540 and outbound UDP 37541 on the host.
- **Link settle**: right after device power-on the PHY link may take a
  moment to come up. Keep resending (every 300 ms) so the first trigger
  after link-up is caught.
- **Both protocols share the same ports** (37541 trigger / 37540 reply),
  so one tool can support uBootEnter.py and BreedEnter with a single flag.
