# JLC rotation check

Method: for each part with an LCSC number, JLC's footprint for that part was downloaded and its pads were
matched by pad number against the KiCad footprint (in library orientation, bottom parts un-flipped), trying
0/90/180/270. **Offset** = the angle to add to the KiCad rotation for JLC's placement file
(field `JLCPCB Rotation Offset`). **Fit** = worst pad mismatch in mm after alignment; small values
(< 0.5 mm) are pad-size differences between libraries, and every result was well clear of the next-best
angle (e.g. SOT-23: 0.28 mm at 180 vs 1.98 mm or more at the other angles).

Pad data source: JLCEDA/EasyEDA Official Library (https://lceda.cn/, https://easyeda.com).

## Status

- 431 footprints, 281 with an LCSC number, 86 distinct LCSC parts.
- 150 footprints have no LCSC number: test points, net ties, jumpers, 1.00/2.54 mm pin headers (not assembled).
- **C100 and C108** (JLCPCB:C_0603, 100nF) have **no LCSC number**, so JLC would not place them.
- Checked: 27 of 86 parts. EasyEDA started refusing requests (HTTP 403) after 25; the rest were not retried.
  C2286 and C910544 were checked from pad data already cached by another project (no new requests).

## Applied to the board

- `JLCPCB Rotation Offset` = 180 on Q1-Q8, U1 and D10, in both the schematic symbols and the PCB footprints
  (field name used by the Fabrication Toolkit plugin; kicad-jlcpcb-tools has an equivalent per-part override).
- C100 and C108: LCSC = C14663, the same part as the other 100nF 0603 caps (their datasheet link already pointed to it).

## Results for checked parts

| LCSC | KiCad footprint | JLC footprint | Offset | Fit (mm) | Refs |
|---|---|---|---|---|---|
| C15127 | JLCPCB:Q_SOT-23 | SOT-23_L2.9-W1.3-P1.90-LS2.4-BR | 180 | 0.28 | Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8 |
| C163219 | Package_TO_SOT_SMD:SOT-23-5 | SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR | 180 | 0.20 | U1 (bottom) |
| C135262 | Inductor_SMD_Wurth:L_Wurth_WE-LQSH-4020 | IND-SMD_L4.0-W4.0_LQH44PN2R2MP0L | 0 | 0.25 | L1 |
| C12530 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C79, C80, C81, C82, C83, C84, C85, C86, C87, C96 |
| C15195 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C39 |
| C1530 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C66 (bottom), C67 (bottom) |
| C1546 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C45, C47 |
| C1548 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C22 |
| C1550 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C15 |
| C15525 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C14 (bottom), C74, C8 (bottom) |
| C1565 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C105 (bottom), C12 |
| C1569 | JLCPCB:C_0402 | C0402 | 0 | 0.08 | C104 (bottom) |
| C14663 | JLCPCB:C_0603 | C0603 | 0 | 0.08 | C11, C13 (bottom), C19, C20, C21, C24 (bottom), C25 (bottom), C27 (bottom), C30 (bottom), C35 (bottom), C37 (bottom), C38, C44, C46, C49, C55, C56, C58 (bottom), C68, C69, C70 (bottom), C71 |
| C15849 | JLCPCB:C_0603 | C0603 | 0 | 0.08 | C16, C17, C18, C23 (bottom), C43, C48 |
| C1631 | JLCPCB:C_0603 | C0603 | 0 | 0.08 | C50, C51 |
| C19666 | JLCPCB:C_0603 | C0603 | 0 | 0.08 | C40 (bottom) |
| C21120 | JLCPCB:C_0603 | C0603 | 0 | 0.08 | C52, C53 |
| C1779 | JLCPCB:C_0805 | C0805 | 0 | 0.05 | C1 |
| C19077554 | JLCPCB:D_SMA | SMA_L4.3-W2.6-LS5.0-RD | 0 | 0.40 | D7 (bottom) |
| C18584 | JLCPCB:H1102NLT_C18584 | XFMR-SMD_H1102NLT | 0 | 0.00 | T1 (bottom) |
| C131280 | JLCPCB:IR-LED_C131280 | LED-SMD_L3.2-W1.6-RD_RED | 0 | 0.00 | D13, D16, D17, D18 |
| C14033 | JLCPCB:L_0402 | L0402 | 0 | 0.08 | L3, L4 (bottom) |
| C17168 | JLCPCB:R_0402 | R0402 | 0 | 0.07 | R21, R85 (bottom), R86 |
| C149504 | JLCPCB:R_0805 | R0805 | 0 | 0.09 | R40 (bottom), R41 |
| C17888 | JLCPCB:R_1206 | R1206 | 0 | 0.02 | R108 (bottom) |
| C2286 | JLCPCB:D_0603 | LED-SMD_L1.6-W0.8-R-RD | 180 | 0.04 | D10 |
| C910544 | JLCPCB:MAX98357AETE+T_C910544 | TQFN-16_L3.0-W3.0-P0.50-BL-EP1.5 | 0 | 0.00 | U15 (bottom) |

**Needs an offset: all SOT-23 Q1-Q8, U1 (SOT-23-5) and the 0603 LED D10 are 180 deg.** Everything else checked is 0.
D12 (C2290, also JLCPCB:D_0603) probably needs 180 as well, but it is not checked yet.

## Not yet checked (59 parts)

About 33 of these are non-polarised 2-pin resistors, capacitors and inductors, where 0 vs 180 makes no
difference. The ones that matter:

- ICs: U7 ESP32-S3 (QFN-56), U18 AXP2101 (QFN-40), U9 W5500 (LQFP-48), U20 PCA9685 (TSSOP-28),
  U21 TCA9555 (TSSOP-24, bottom), U11 TM1637, U12 LM75B, U13 W25Q128 (SOIC-8),
  U14 DP9900M, U3 USBLC6 (SOT-23-6, bottom), U5/U6 XC6206 (SOT-23-3)
- Modules and connectors: U19 RFM95 (bottom), FPC1 camera connector (bottom), J1 USB-C (bottom), CARD1,
  J9 RJ45, CN1 audio jack, BT1 18650 holder, DISP1 (bottom)
- Polarised small parts: D9 WS2812 (bottom), U16/U17 mics (bottom), D5/D6 MB6S (bottom),
  D1/D11 DSK24 (bottom), LEDs D3/D4/D8/D12/D14/D15, X1/Y1 oscillators, ANT1, ANT3 (bottom),
  buttons PB1-PB5/RST2/BOOT1
