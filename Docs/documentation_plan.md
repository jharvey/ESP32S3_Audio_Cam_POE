# Documentation plan

Two sets of documents. **Development documents** are for the people building the intercom; they change with the
work and live next to the code. **Final documents** are for the school and anyone building or installing units;
each is frozen and versioned with a release (R1, R2, ...). Status: **draft** = written, not checked against
hardware; **living** = kept current as work goes on; **to write**; **frozen at R1** etc.

## Development documents

| Document | What | Status |
|---|---|---|
| [roadmap.md](roadmap.md) | Every feature by release and increment, with status | living |
| [release_designs.md](release_designs.md) | Designs for the coming increments | living |
| [README.md](README.md) (decisions) | Decision log | living |
| Architecture (`firmware/README.md` + to write: `dev_architecture.md`) | Core / HAL / board / simulator split, threads, data flow per frame | to write |
| Protocol spec (`dev_protocol.md`) | Packet formats (audio, status v2, commands, replies), multicast, signing; from `Packet.h` / `Auth.cpp` | to write |
| Build and test (`dev_build_test.md`) | Firmware build (VM100, Windows NAS toolchain), simulator (Qt, libopus), `ic-selftest`, `ic-netsim`, `record-walkthroughs.ps1` | to write |
| Release process (`dev_release.md`) | Versioning, `unit_tool.py release`, release folders, SD-card file, what's tested before a release | to write |
| [echo_prediction.md](echo_prediction.md) | Echo from the board layout; between rooms | living |
| [network_sim_results.md](network_sim_results.md) | Network load test | done |
| Hardware: [single_sided_layout.md](single_sided_layout.md), [routing_handoff.md](routing_handoff.md), [case_and_cost_notes.md](case_and_cost_notes.md), [bring-up.md](bring-up.md) | Layout, routing state, case and cost, first-board checks | living |
| Routing tools (VM105 scripts, kicad-rbr trial) | How the board was routed | in routing_handoff.md |

## Final documents (per release)

| Document | For | Status |
|---|---|---|
| [teacher_guide.md](teacher_guide.md) | Teachers | draft (frozen at R1) |
| Code card (printable, one page) | By each unit: display codes, TALK, CALL | to write (from the teacher's guide) |
| [office_guide.md](office_guide.md) | The office | draft (frozen at R1, extended at R2 / R3) |
| [install_guide.md](install_guide.md) | Installers / IT | draft (frozen at R1) |
| [maintenance_guide.md](maintenance_guide.md) | Upkeep | draft (frozen at R1) |
| Quick start (one page) | Setting up a single unit | to write |
| Product sheet | Features, power (PoE class), network needs, audio, camera, sizes | to write |
| Release notes / changelog | What changed in each firmware release | to write (from R1) |
| Manufacturing pack | Gerbers, drill, BOM and CPL for JLC, assembly notes (hand-fitted parts), case STL / STEP | to write (when the board is final) |
| Safety, privacy and network notes | Laser (Class 2 only), camera privacy (cover, emergency only), intercom VLAN, PoE | to write |
| Video walkthroughs (`Docs/videos/`) | Everyone: all-call, gym group, duplicate number, auto-tune, portable unit | recorder ready, videos to record |

## Rules

- Final documents only describe what the release does; planned features stay in the roadmap (or are marked
  *(planned)* while a guide is still a draft).
- At each release: freeze the final documents with the release number in their title, record the videos again
  from that firmware, and write the release notes.
- Development documents link to code (file paths), final documents don't.
