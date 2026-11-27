# JLCPCB order report

Made by `tools/jlc_pack.py`. Prices are list prices from a public parts index; stock is not checked. Confirm in JLCPCB's quote.

- Parts placed by JLCPCB (top side): **288**, 86 different part numbers: 44 basic, 9 preferred, 33 extended.
- Placed parts with no LCSC number: none
- Fitted by hand: 4 items (see `hand_fit_lcsc.csv`).

| Order size | Placed parts, each | Extended fees, each | Setup + stencil, each | Hand-fit parts, each | Electronics, each (no bare board, no shipping) |
|---|---|---|---|---|---|
| 5 | $38.38 | $19.80 | $1.90 | $9.24 | **$69.32** |
| 45 | $38.38 | $2.20 | $0.21 | $9.24 | **$50.03** |

## Extended parts (each adds a one-time fee per order, about $3)

| LCSC | Part | Qty | Used at |
|---|---|---|---|
| C2844473 | DL-RFM95-915M | 1 | U19 |
| C2678753 | PCA9685PW,118 | 1 | U20 |
| C2913194 | ESP32-S3R8 | 1 | U7 |
| C32843 | W5500 | 1 | U9 |
| C48227730 | MSM261DGT003 | 2 | U16 U17 |
| C2913085 | SLS0563DBA1GD | 1 | DISP1 |
| C3036461 | AXP2101 | 1 | U18 |
| C465732 | TCA9555PWR | 2 | U21 U22 |
| C910544 | MAX98357AETE+T | 1 | U15 |
| C111829 | SY6288AAAC | 7 | Q2 Q3 Q4 Q5 Q6 Q7 Q8 |
| C33099 | LM75BDP,118 | 1 | U12 |
| C5370061 | H2U646MHLQ0200 | 1 | ANT3 |
| C82584 | MB6S | 2 | D5 D6 |
| C435392 | DFE252012F-1R0M=P2 | 5 | L5 L6 L7 L8 L9 |
| C18584 | H1102NLT | 1 | T1 |
| C5337160 | TM1637(TA2007) | 1 | U11 |
| C707223 | PJ-316C-6A | 1 | CN1 |
| C177225 | 532610271 | 1 | J20 |
| C262276 | AFC01-S24FCC-00 | 1 | FPC1 |
| C9010 | X322540MPB4SI | 1 | Y1 |
| C163219 | TLV62568DBVR | 1 | U1 |
| C2765186 | TYPE-C 16PIN 2MD(073) | 1 | J1 |
| C393941 | TF PUSH | 1 | CARD1 |
| C5349954 | XL-1615RGBC-2812B | 1 | D9 |
| C2925417 | KH-3216-H0209 | 1 | ANT1 |
| C135262 | SMNR4020-2.2UH | 1 | L1 |
| C131280 | IR26-21C/L110/CT | 1 | D13 |
| C3018545 | B57230V2103F260 | 1 | NTC1 |
| C41424780 | XC6206P282MR-G | 1 | U6 |
| C2827654 | USBLC6-2SC6 | 1 | U3 |
| C29682 | SDCL1608C2N2STDF | 1 | L2 |
| C2984869 | XC6206-1.5NR | 1 | U5 |
| C2293 | KT-0805B | 1 | D14 |

## Printed parts

Total 141 cm3 per unit (142 g in nylon). In MJF nylon at $0.275/g that is about **$39 per unit**; the two case halves are 97% of it.

## Check these turns in JLCPCB's placement preview


