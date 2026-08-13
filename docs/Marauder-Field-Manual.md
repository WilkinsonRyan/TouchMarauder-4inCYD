# Marauder Field Manual — Plain English

Every menu item explained: what it does, what appears on screen, and what you can
actually do with the results — no jargon assumed.

> ⚠ **Read this first.** This device is for learning, and for testing networks and
> gadgets you **own or have written permission to test**. The "watch" features are
> passive and harmless. The "transmit" features — deauth, Evil Portal, Bluetooth
> spam — disrupt other people's devices and are illegal to point at anyone else.
> Treat it like a lock-picking set: fine on your own locks, not your neighbor's.

Legend: **[WATCH]** = passive / receive-only / safe · **[TRANSMIT]** = active /
disruptive / authorized-use only.

---

## 00 — How the whole thing works

Almost everything follows the same three-step rhythm. Most attacks work on the
specific networks or devices **you picked** from a scan, not on "everything."

1. **Scan (watch)** — run a sniffer (e.g. "Scan APs"); the device fills a list
   with what it hears around you.
2. **Select** — open that list and tap your targets (a checkmark = "this one").
3. **Act** — run an attack; it only affects what you selected.

Two words that unlock the menu: an **Access Point (AP)** is a WiFi network /
router — the named thing you connect to. A **Station** is a device connected to
one (phone, laptop, TV). Attacks usually target one or the other.

**Where captured data goes:** anything recorded (packet captures, handshakes) is
written as a `.pcap` file to the **microSD card**, which you open on a computer.
No card = the watch tools still show live info on screen, they just can't save it.

---

## 01 — WiFi: Watching (Sniffers)

These only listen. Nothing here disrupts anyone — you're reading what devices
already broadcast into the open air.

### Scan APs — [WATCH]
Lists nearby WiFi networks (routers). Your **starting point** — most WiFi attacks
act on APs you select here.
- **On screen:** a growing list — network name (SSID), channel, signal, MAC.
- **So what:** tap the ones to target, then go to Attacks; or just see who's around.

### Scan Stations / Scan All — [WATCH]
Lists **devices** connected to nearby networks. "Scan All" finds networks + their
devices together.
- **So what:** target a single device — e.g. knock only your laptop off your WiFi.

### Probe Request Sniff — [WATCH]
Phones constantly call out for networks they've joined before ("Is HomeWiFi
here?"). Those calls are **probe requests**; this shows them live.
- **So what:** a privacy eye-opener — you see the names of places nearby phones
  connected to before.

### Beacon Sniff — [WATCH]
Routers shout "I'm here, my name is ___" many times a second — **beacons**. Like
Scan APs, but watching the announcements themselves.

### Detect Pwnagotchi — [WATCH]
Finds nearby **Pwnagotchi** gadgets (a pocket device that collects WiFi
handshakes) by their signature.

### Packet Monitor — [WATCH]
A live graph of **all** WiFi traffic around you — a busy-ness meter for the air.

### Channel Analyzer — [WATCH]
Shows which WiFi channels (1–14) are busiest.
- **So what:** find a quiet channel for your own router.

### Signal Monitor — [WATCH]
A live signal-strength meter — walk around and the number rises as you get closer.
- **So what:** "hot / cold" — physically locate a router or hidden transmitter.

### Deauth Sniff — [WATCH]
Watches for **deauthentication** frames — the sign that something is kicking
devices off WiFi.
- **So what:** defensive — detect whether a deauth attack is happening near you.

### EAPOL / PMKID Scan — [WATCH]
When a device joins a password-protected (WPA/WPA2) network, the two do an
encrypted **handshake**. This captures it — the full four-step handshake (EAPOL)
or a shortcut value the router offers (PMKID).
- **On screen:** a counter that ticks up per handshake; each saved as `.pcap` on SD.
- **So what:** you **cannot** read the password from it. But on a computer,
  **hashcat** can test how strong the password is by trying guesses against it
  *offline*. That's how you audit whether your own WiFi password would survive.
- ⚠ **Authorized only** — only capture/crack handshakes for networks you own or
  are hired to test.

### Raw Capture — [WATCH]
Records **everything** it hears into one `.pcap` on SD.
- **So what:** the general-purpose "record it, I'll look later" button. Open in
  **Wireshark** on a computer.

---

## 02 — WiFi: Transmitting (Attacks)

These broadcast into the air and **affect other devices**. Point them only at
your own gear.

### Rickroll Beacon — [TRANSMIT]
Broadcasts 8 fake networks named after the song's chorus ("01 Never gonna give
you up" … "08 and hurt you"). Sends all 8, in order, looping many times per
second — but one radio can only announce one at a time.
- **Expect:** nearby phones show a *rotating handful* in the phone's own order —
  seeing just "07"/"08" is normal, not a bug. It won't reliably start at "01".

### Beacon Spam — List / Random — [TRANSMIT]
Same idea, with names from a list you set (List) or random gibberish (Random).
A demo/prank — annoying, not damaging.

### Probe Req Flood — [TRANSMIT]
Floods the air with fake "is network X here?" requests.

### Deauth (flood) — [TRANSMIT]
Sends deauth frames broadly — nearby devices drop off WiFi and must reconnect.
- ⚠ Jamming WiFi you don't own is illegal (denial-of-service). Use on your own.

### Deauth Targeted — [TRANSMIT]
The precise version. Scan APs → select **one** network → this kicks devices off
*only that network*.
- **So what:** the go-to for testing your own network's resilience.

### AP Mimic — [TRANSMIT]
Re-broadcasts the exact names of real networks **you selected** from a scan.
- **Workflow:** Scan APs → select networks to copy → Attacks → AP Mimic.

### AP Spam — [TRANSMIT]
Blasts a large number of fake access points at once.

### Evil Portal — [TRANSMIT]
Creates a fake WiFi network with a captive-portal login page (the "sign in to
continue" screen). Whatever someone types, the device records.
- **On screen:** hosts a network + web page (HTML from SD, or a built-in default).
- **So what:** the classic phishing-awareness demo.
- ⚠ **Consent required** — capturing others' logins without permission is illegal.

---

## 03 — Bluetooth: Watching

Same idea as the WiFi sniffers, for the Bluetooth world. Passive and safe.

### Bluetooth Sniff — [WATCH]
Lists nearby BLE devices — earbuds, watches, bands, trackers, some phones.

### Bluetooth Analyzer — [WATCH]
An activity graph for Bluetooth traffic.

### Detect Flipper — [WATCH]
Spots nearby **Flipper Zero** devices by their Bluetooth signature.

### AirTag Sniff — [WATCH]
Detects nearby Apple **AirTags** / Find-My beacons.
- **So what:** anti-stalking — check whether an unknown tag is traveling with you.

### Detect Card Skimmers — [WATCH]
Looks for the Bluetooth signatures of common **credit-card skimmer** modules.
- **So what:** genuinely useful and defensive — sweep a gas pump before you tap.

---

## 04 — Bluetooth: Transmitting

Fake Bluetooth pop-ups. Mostly "annoying prank" territory — aim at your own phones.

### Sour Apple — [TRANSMIT]
Spams nearby iPhones/iPads with fake "connect this accessory?" pop-ups.

### Swift Pair / Samsung / Google Spam — [TRANSMIT]
The same trick for Windows (Swift Pair), Samsung, and Android/Google phones.

### Flipper Spam / Spam All — [TRANSMIT]
Flipper-flavored fake ads; "Spam All" runs every kind at once.
- ⚠ These interrupt strangers' phones — demo on devices you control.

### Spoof AirTag — [TRANSMIT]
Broadcasts fake AirTag / Find-My beacons.

---

## 05 — What do I do with what I capture?

### Any `.pcap` → Wireshark
"PCAP" = packet capture, the standard recording format. Every Raw Capture,
EAPOL/PMKID, and packet-monitor save is a `.pcap` on your SD card.
1. Move the card to a computer.
2. Open the file in **Wireshark** (free, wireshark.org) — it turns the raw
   recording into a readable, filterable list of everything in the air.
3. Filter by device, type, or network to make a giant capture manageable.

### A handshake → hashcat (password auditing)
The EAPOL/PMKID capture is the raw material for a WiFi **password strength test**.
On a computer, **hashcat** (free) tries millions of guesses against the handshake,
entirely offline. Fast fall = weak; survives = strong.
- ⚠ Doing this to **your own** network is a security audit; to someone else's it's
  breaking in. Same tool, completely different legality.

### A scan → a target list
The most common "what next" isn't a file — it's **selecting** rows from a scan and
feeding them to an attack (Deauth Targeted, AP Mimic).

---

## 06 — Your device's own features

- **Toggle Theme** (Settings) — Light / Dark / Hacker / Pride, remembered across
  reboots. Hacker = neon-green + Matrix rain; Pride = rainbow menu items.
- **Brightness +/−** and **Calibrate Touch** (Settings).
- **Idle screensaver** — after ~25s the screen dims and plays a themed animation
  with cycling quotes; tap to wake.
- **TouchBoard** — the main-menu tile reboots into the on-screen Bluetooth
  keyboard; its "Exit to Marauder" button (BT tab) returns.
- **microSD** — FAT32 card holds captures, Evil Portal pages, and saved scans.

---

## 07 — Glossary

| Term | Plain meaning |
|------|---------------|
| **AP** | Access Point — a WiFi network / router. |
| **Station** | A device connected to an AP (phone, laptop, TV); a "client." |
| **SSID** | The network's name. |
| **MAC / BSSID** | A device's unique hardware address. |
| **Beacon** | The "I'm here" announcement a router broadcasts constantly. |
| **Probe request** | A phone asking "is this saved network nearby?" |
| **Deauth** | A "you're disconnected" message; spammed, it kicks devices off WiFi. |
| **Handshake** | The encrypted exchange when a device joins a secured network. |
| **EAPOL / PMKID** | The two forms of that handshake Marauder can grab. |
| **Channel** | One of the WiFi "lanes" (1–14). |
| **RSSI / signal** | How strong a signal is — closer to 0 = physically closer. |
| **BLE** | Bluetooth Low Energy — what most modern gadgets/trackers use. |
| **PCAP** | A packet-capture file; open it in Wireshark. |
| **Captive portal** | The "sign in to use this WiFi" page; Evil Portal fakes one. |

---

## 08 — Where to learn more (and real screenshots)

- **[Official Marauder Wiki](https://github.com/justcallmekoko/ESP32Marauder/wiki)** — feature docs + screenshots from the original author.
- **[JustCallMeKoko (YouTube)](https://www.youtube.com/@justcallmekoko)** — video walkthroughs of the tools running.
- **[Wireshark](https://www.wireshark.org/)** — open and read your `.pcap` captures.
- **[hashcat](https://hashcat.net/hashcat/)** — audit password strength from a captured handshake.

*Use it to learn, and to test only what's yours or what you're authorized to test.
When in doubt, don't transmit.*
