# ProtoPirate

> Flipper Zero FAP · Car key fob RF decoder, analyzer & emulator for Sub-GHz

![Version](https://img.shields.io/badge/version-3.1-blue)
![API](https://img.shields.io/badge/Flipper%20API-87.x-orange)
![Category](https://img.shields.io/badge/category-Sub--GHz-purple)
![Protocols](https://img.shields.io/badge/protocols-27%2B-green)
![License](https://img.shields.io/badge/license-GPL--3.0-red)

---

## Übersicht

ProtoPirate dekodiert, analysiert und emuliert Automotive Sub-GHz Signale von Fahrzeugschlüsseln (Keyfobs). Die App unterstützt 27+ Protokollfamilien über AM- und FM-Modulation.

> **Nur für autorisierte Sicherheitstests und eigene Fahrzeuge. Die Nutzung an Fahrzeugen ohne Genehmigung ist illegal.**

---

## Features

| Feature | Beschreibung |
|---|---|
| **Receive** | Echtzeit-Empfang und Dekodierung von Sub-GHz Keyfob-Signalen mit Radar-Animation |
| **Saved Captures** | Gespeicherte `.psf` Captures laden, anzeigen, emulieren |
| **Sub Decode** | `.sub` Dateien von der SD-Karte analysieren und dekodieren |
| **Timing Tuner** | Protokoll-Timing analysieren und mit Protokolldefinition vergleichen |
| **Emulate** | Decodierte Signale zurücksenden (Transmission aktivierbar in About) |
| **PSA Brute Force** | PSA (Peugeot/Citroën) Brute-Force Angriff (Plugin) |
| **Frequency Hopping** | Automatisches Durchsuchen mehrerer Frequenzen |
| **Auto-Save** | Empfangene Signale automatisch speichern |

---

## Unterstützte Protokolle

### AM-Protokolle

| Protokoll | Decoder | Encoder | Encoding | Verschlüsselung | CRC | Frequenz |
|---|---|---|---|---|---|---|
| Chrysler V0 | ✅ | ✅ | PWM | Rolling Code | Checksum | 315 / 433.92 MHz |
| Fiat V0 | ✅ | ✅ | Manchester | Rolling Code (static emu) | — | 315 / 433.92 MHz |
| Fiat V1 | ✅ | — | Manchester | Rolling Code | CRC8 | 315 / 433.92 MHz |
| Ford V0 | ✅ | ✅ | Manchester | Rolling Code | CRC+Checksum | 315 / 433.92 MHz |
| Honda V1 | ✅ | ✅ | Manchester | Rolling Code | CRC4 | 315 / 433.92 MHz |
| Kia V1 | ✅ | ✅ | Manchester | Rolling Code | CRC4 | 315 / 433.92 MHz |
| Kia V2 | ✅ | ✅ | Manchester | Rolling Code | CRC4 | 315 / 433.92 MHz |
| Porsche/Touareg | ✅ | — | PWM | Rolling Code | — | 315 / 433.92 MHz |
| PSA (Peugeot/Citroën) | ✅ | ✅ | Manchester | XTEA/XOR | CRC8 | 315 / 433.92 MHz |
| StarLine | ✅ | ✅ | PWM | KeeLoq | — | 315 / 433.92 MHz |
| Subaru | ✅ | ✅ | PPM | Rolling Code | — | 315 / 433.92 MHz |
| VAG (VW/Audi/Seat/Skoda) | ✅ | ✅ | Manchester | AUT64/XTEA | — | 434.42 MHz |

### FM-Protokolle

| Protokoll | Decoder | Encoder | Encoding | Verschlüsselung | CRC | Frequenz |
|---|---|---|---|---|---|---|
| Ford V1 | ✅ | ✅ | Manchester | Rolling Code | CRC16 | 315 / 433.92 MHz |
| Ford V2 | ✅ | ✅ | Manchester | Rolling Code (simple replay) | — | 434.25 MHz |
| Ford V3 | ✅ | — | Manchester | Rolling Code | — | 434.25 MHz |
| Honda Static | ✅ | ✅ | PWM | Static Code | Checksum | 315 / 433.92 MHz |
| Kia V0 / Suzuki / Honda V0 | ✅ | ✅ | PWM | Rolling Code | CRC8 | 315 / 433.92 MHz |
| Kia V3 / V4 | ✅ | ✅ | PWM | KeeLoq | CRC4 (BF) | 315 / 433.92 MHz |
| Kia V5 | ✅ | ✅ | PWM | Rolling Code | ✅ | 315 / 433.92 MHz |
| Kia V6 | ✅ | ✅ | Manchester | AES128 | CRC8 | 315 / 433.92 MHz |
| Kia V7 | ✅ | ✅ | Manchester | Rolling Code | CRC8 | 315 / 433.92 MHz |
| Land Rover V0 | ✅ | ✅ | PWM | Rolling Code | Check+Tail | 315 / 433.92 MHz |
| Mazda V0 | ✅ | ✅ | Manchester | Rolling Code | Checksum | 315 / 433.92 MHz |
| Mitsubishi V0 | ✅ | — | PWM | Rolling Code | — | 315 / 433.92 MHz |
| PSA FM | ✅ | ✅ | Manchester | XTEA/XOR | CRC8 | 315 / 433.92 MHz |
| Scher-Khan | ✅ | — | PWM | Magic Code | — | 315 / 433.92 MHz |

---

## Installation

### Option A — Direkt auf SD-Karte
1. `protopirate.fap` aus dem [Release](https://github.com/G4MEOVER18/ProtoPirate/releases/latest) herunterladen
2. Auf SD-Karte nach `/apps/Sub-GHz/protopirate.fap` kopieren
3. Flipper → Apps → Sub-GHz → ProtoPirate

### Option B — ufbt bauen
```bash
ufbt
# Direkt deployen:
ufbt launch
```

---

## Build-Flags (defines.h)

| Flag | Standard | Funktion |
|---|---|---|
| `ENABLE_SUB_DECODE_SCENE` | ✅ | Sub Decode Scene (`.sub` Datei-Analyse) |
| `ENABLE_TIMING_TUNER_SCENE` | ✅ | Timing Tuner für Protokoll-Entwicklung |
| `ENABLE_EMULATE_FEATURE` | ✅ | TX/Emulate-Funktion |
| `REMOVE_LOGS` | ✅ | Entfernt FURI_LOG im Release-Build |

---

## Kompatibilität

| Firmware | Status |
|---|---|
| G4MEOVER-FW v1.0.0 | ✅ |
| Momentum mntm-012 | ✅ |
| Unleashed | ✅ |
| Official Firmware | ✅ |

---

## Companion: G4MEOVER-FW

Optimiert für **[G4MEOVER-FW](https://github.com/G4MEOVER18/G4MEOVER-FW)** Custom Firmware.  
Für UART-basierte Remote-Steuerung via Heltec ESP32: **[lora-ukfe](https://github.com/G4MEOVER18/lora-ukfe)**

---

## Credits

**App Development:** RocketGod · MMX · Leeroy · gullradriel · Skorp  
**Protocol Research:** L0rdDiakon · YougZ · DoobTheGoober · Slackware · Trikk · Wootini · Li0ard · Ash  
**Reverse Engineering:** DoobTheGoober · MMX · NeedNotApply · RocketGod

Original von **The Pirates' Plunder**

---

## Support

[![PayPal](https://img.shields.io/badge/PayPal-Spenden-0070ba?style=flat-square&logo=paypal)](https://paypal.me/Freakbank1)
[![Bitcoin](https://img.shields.io/badge/BTC-39vZWmnUwDReQ15BwqQXzyqVQ6U8LardEf-f7931a?style=flat-square&logo=bitcoin)](bitcoin:39vZWmnUwDReQ15BwqQXzyqVQ6U8LardEf)

```
BTC: 39vZWmnUwDReQ15BwqQXzyqVQ6U8LardEf
```

---

*Teil der G4MEOVER Security Toolchain · [github.com/G4MEOVER18](https://github.com/G4MEOVER18)*
