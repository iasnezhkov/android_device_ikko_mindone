# libmtk-ril.so AT command / URC recovery notes

Target: `vendor/lib64/libmtk-ril.so` inside the LineageOS build tree guest (`lineage`),
ELF64 aarch64, 4,833,016 bytes, dynamic-symbol table intact (10,703 dynsym entries,
10,163 defined), everything else stripped. Read-only analysis only; nothing was
copied out of the build tree except small text/section extracts, and the tree was
never modified.

## Method (for citing in the main project doc)

1. **Symbol table.** `readelf -sW --dyn-syms libmtk-ril.so | c++filt` gives every
   exported symbol with its address *and size* (Android's toolchain does emit
   `st_size` for these, so no "next symbol" guessing was needed — all 1,026
   `Rmc*RequestHandler::*` `FUNC` symbols found have a real, non-zero size).
2. **Sections.** `readelf -SW` gives `.text = [0x1e6000, 0x460204)` and
   `.rodata = [0xffd30, 0x18c11c)` (flags `AMS` — alloc+merge+**strings**, i.e. this
   section *is* the C-string literal pool). For this binary, section VMA equals file
   offset for both `.text` and `.rodata` (delta 0, confirmed from `readelf -SW`), so
   no offset translation was needed to read rodata bytes straight out of the file at
   their VMA. (`.data`, by contrast, has a 0x1000 VMA/offset skew — irrelevant here
   since nothing we needed lived there.)
3. **Disassembly.** `llvm-objdump -d --no-show-raw-insn --start-address=0x1e6000
   --stop-address=0x460204 libmtk-ril.so` once, over the whole `.text` section
   (649,345 instruction lines, ~1 second to run, 30 MB of text) — far cheaper than
   invoking objdump per-symbol 1,026 times. `--symbolize-operands` was tested and is
   supported by this LLVM 21 objdump build, but it adds nothing useful for literal
   pool addresses (no rodata string annotation), so plain `-d` was used.
4. **Per-method slicing.** Each method's disassembly is `[start, start+size)` sliced
   out of that one big listing (binary search on address). No guessing of function
   boundaries was required (see point 1).
5. **adrp/add/ldr cross-reference.** A small Python state machine walks each
   method's instructions: on `adrp xN, #page` it remembers `page` for register `N`;
   on the next `add xN, xN, #imm` (or `ldr`/`ldrb xM, [xN, #imm]`) touching that same
   register, it computes `resolved = page + imm`. This is exactly the idiom the task
   describes, applied mechanically instead of by eye — verified by hand against the
   raw disassembly for the sample methods below, all of which matched.
6. **String resolution.** If `resolved` falls inside the `.rodata` VMA range, the
   NUL-terminated string is read from a local byte-for-byte copy of that one section
   (574,444 bytes — not the executable itself, just its string pool, extracted once
   via a Python read+stdout-pipe, no file ever written inside the tree).
7. **Classification.** A string is counted as an **AT command** if it matches
   `^AT[+\-&A-Z0-9]` (covers both the proprietary `AT+E...`/`AT+STK...` family and
   plain Hayes commands like `ATD`, `ATE0`, `ATV0`). A string is counted as a
   **URC/response-prefix candidate** if it matches `^\+[A-Z][A-Z0-9]{1,15}(:|=|$)`
   (e.g. `+CREG:`, `+ESMLRSU: 3,1,...`). **Caveat, stated plainly:** static
   adrp/add cross-referencing cannot distinguish "the handler sends this AT command"
   from "the handler checks the `+XXX:` prefix on the *response* to a command it just
   sent" — both are literal `+XXX` strings referenced the same way. Genuinely
   *unsolicited* URC parsing (async notification from the modem, not tied to a
   request/response pair) mostly lives in separate `Rmc*UrcHandler` classes that are
   **outside** the 37/39-class list given for this task, so the `urcs_or_response_prefixes`
   field here should be read as "prefix strings this handler references", not a
   guarantee that all of them are asynchronous URCs.
8. **Confidence.** `high` = a direct `adrp+add` hit resolving to a kept AT/URC
   string, in a method of ≤150 instructions (small enough to trust the automated
   scan matches what a human reading would find). `medium` = same but in a larger
   method, or resolved only via the indirect `adrp+ldr` form. `low` = no AT/URC
   string found at all (either the method genuinely doesn't reference one — a getter,
   ctor/dtor boilerplate, dispatcher — or it's large/complex enough that absence
   isn't conclusive). A handful of methods (listed with
   `"manually_spot_verified": true` in the JSON) were additionally re-read from the
   raw disassembly by hand and forced to `high`; everything else Tier-A got the
   automated pass applied to **every** method (623 methods, this *is* the "full
   per-method cross-referencing" the task asked for — done at scale rather than
   one-by-one, and spot-checked). Tier-B got the same mechanical pass for coverage,
   but was not hand-verified beyond the handful flagged in the JSON.
9. **Boot-handshake token hunt.** `strings`-equivalent regex scan (`AT\+E[A-Z0-9]+`)
   over the whole 574 KB `.rodata` copy found **256 distinct `AT+E...` tokens**. The
   5 task-guessed tokens were checked by exact substring match; the 3 that exist were
   then traced back to their sending function via a *global* (all-classes, not just
   the 37/39) adrp/add scan matching their exact resolved address, then the
   containing function was looked up in the full (not just Rmc*) symbol table.

## Handler class count correction

The task prompt says "37 handler classes" but the list it gives has **39** distinct
names (recounted by hand and cross-checked against `nm -DC`: all 39 exist and export
real methods). All 39 were processed; 20 match the prompt's explicit Tier-A list, the
remaining 19 got the Tier-B treatment.

## Summary table

| Handler | Tier | Methods | Methods w/ AT cmd | Unique AT cmds | high/med/low |
|---|---|---:|---:|---:|---|
| RmcAtciCommonRequestHandler | A | 5 | 0 | 0 | 0/0/5 |
| RmcAtciRequestHandler | A | 3 | 1 | 2 | 0/1/2 |
| RmcCallControlChldRequestHandler | A | 16 | 9 | 10 | 9/0/7 |
| RmcCallControlCommonRequestHandler | A | 43 | 24 | 37 | 18/8/17 |
| RmcCallControlSpecialRequestHandler | A | 10 | 4 | 3 | 4/0/6 |
| RmcCatCommonRequestHandler | A | 26 | 8 | 22 | 5/3/18 |
| RmcCommSimRequestHandler | A | 125 | 79 | 115 | 11/66/48 |
| RmcCommSmsRequestHandler | A | 6 | 2 | 7 | 2/0/4 |
| RmcGsmSimRequestHandler | A | 19 | 9 | 16 | 0/9/10 |
| RmcGsmSmsRequestHandler | A | 25 | 19 | 29 | 3/16/6 |
| RmcHardwareConfigRequestHandler | A | 10 | 0 | 0 | 0/0/10 |
| RmcModemRequestHandler | A | 9 | 2 | 4 | 0/2/7 |
| RmcNetworkRealTimeRequestHandler | A | 30 | 4 | 7 | 1/3/26 |
| RmcNetworkRequestHandler | A | 121 | 90 | 156 | 35/54/32 |
| RmcOemRequestHandler | A | 51 | 21 | 33 | 10/12/29 |
| RmcPhbRequestHandler | A | 40 | 22 | 28 | 4/17/19 |
| RmcPhbSimIoRequestHandler | A | 8 | 1 | 3 | 0/1/7 |
| RmcRadioRelatedRequestHandler | A | 8 | 2 | 5 | 1/1/6 |
| RmcRadioRequestHandler | A | 36 | 17 | 47 | 12/5/19 |
| RmcSuppServRequestHandler | A | 32 | 9 | 20 | 3/6/23 |
| **Tier A total** | | **623** | **323** | (see JSON, cmds overlap across classes) | 118/206/299 |
| RmcAtciSpecialRequestHandler | B | 4 | 0 | 0 | 0/0/4 |
| RmcBaseRequestHandler | B | 7 | 0 | 0 | 0/0/7 |
| RmcCallControlImsRequestHandler | B | 19 | 16 | 16 | 13/3/3 |
| RmcCapabilitySwitchRequestHandler | B | 34 | 7 | 17 | 5/2/27 |
| RmcCdmaSimRequestHandler | B | 12 | 5 | 7 | 0/5/7 |
| RmcElRequestHandler | B | 10 | 2 | 24 | 0/1/9 |
| RmcEmbmsRequestHandler | B | 46 | 24 | 45 | 4/20/22 |
| RmcGbaRequestHandler | B | 8 | 1 | 1 | 0/1/7 |
| RmcGwsdRequestHandler | B | 12 | 0 | 0 | 0/0/12 |
| RmcImsControlRequestHandler | B | 22 | 16 | 24 | 9/7/6 |
| RmcImsProvisioningRequestHandler | B | 8 | 2 | 3 | 2/0/6 |
| RmcImsRttControlRequestHandler | B | 10 | 5 | 4 | 5/0/5 |
| RmcImsVideoRingtoneRequestHandler | B | 7 | 1 | 2 | 0/1/6 |
| RmcMobileWifiRequestHandler | B | 24 | 14 | 14 | 12/2/10 |
| RmcNetworkNrtRequestHandler | B | 16 | 8 | 7 | 1/6/9 |
| RmcVsimRequestHandler | B | 22 | 6 | 17 | 3/3/16 |
| RmcWifiRelayRequestHandler | B | 10 | 4 | 4 | 3/1/6 |
| RmcWpModemRequestHandler | B | 9 | 0 | 0 | 0/0/9 |
| RmcWpRequestHandler | B | 19 | 4 | 8 | 4/0/15 |
| **Tier B total** | | **299** | **115** | | 61/50/188 |
| **Grand total** | | **922** | **438** | **684 globally unique AT cmds, 138 unique URC/prefix strings** | 179/256/487 |

(`method_count` here is after de-duplicating identical `(symbol, address)` pairs —
Android's dynsym lists a number of constructors/methods twice as versioned aliases at
the same address; the raw `nm -DC` count including those duplicates and non-FUNC
`OBJECT` symbols was 1,045.)

## Boot-handshake findings

Checked the 5 task-guessed tokens by exact literal search over the full 574,444-byte
`.rodata` string pool:

| Token | Found? | Sent by |
|---|---|---|
| `AT+ESIMS` | **YES** (`AT+ESIMS=1`) | `RmcRadioRequestHandler::RmcRadioRequestHandler(int,int)` — the **constructor** |
| `AT+EIND` | NO | — not present anywhere in the binary |
| `AT+EMDSTATUS` | NO | — not present anywhere in the binary |
| `AT+ECSRA` | **YES** (`AT+ECSRA=1`) | `RmcWpRequestHandler::worldPhoneInitialize(int)` |
| `AT+ESBP` | **YES** (`AT+ESBP=7,"SBP_GEMINI_LG_WG_MODE"` as a query; also two SET forms in the boot ctor, see below) | `RmcCapabilitySwitchRequestHandler::queryKeep3GMode()` (query) + `RmcRadioRequestHandler` ctor (set) |

**The real find:** `RmcRadioRequestHandler`'s constructor (`RmcRadioRequestHandler(int
slotId, int mainSim)`, address `0x424be4`, ~2000 bytes / ~500 instructions) is a
genuine boot-time AT handshake — it fires off, in order:

```
AT+EBOOT=1
AT+CMEE=1
AT+CMER=1,0,0,2,0
AT+ICCID=1
AT+ESIMS=1
AT+ECFGSET="disable_sms","0"   (or "1", branch on RfxRilUtils::isSmsSupport())
AT+ESBP=5,"SBP_ETWS_FOR_AOSP_DESIGN",1
AT+ESBP=5,"SBP_TERMINAL_CAPABILITY_FLEX",1
AT+CSCS="UCS2"
AT+EDSDA=1
AT+EPOC
AT+EFUN=0
AT+ESLOTSINFO=1
```
plus calls to `sendERAT()` / `sendEGRAT()` (own methods of the same class), which
send `AT+ERAT=%d` / `AT+EGRAT=%d` respectively (radio-access-technology bring-up).

So the task's premise (proprietary handshake at init) is correct, but **the actual
init-time AT command is `AT+EBOOT=1`**, not any of the 5 guessed names, and it's sent
from the `RmcRadioRequestHandler` **constructor**, not a `requestXxx()` method — this
handler is instantiated once per SIM slot at RIL bring-up, and its constructor body
*is* the boot handshake. `AT+ESIMS=1` (guessed correctly) is real but is just one line
in that same sequence, not a separate/isolated handshake step.

`AT+ECSRA=1` and the `AT+ESBP=5,...` variants are also part of world-phone /
capability bring-up but live in two different handler classes
(`RmcWpRequestHandler::worldPhoneInitialize`, called separately from RIL/world-phone
init) — `RmcWpRequestHandler` also exports a data object `ecsraUrcParams` (20 bytes)
that looks like a parameter table tied to the ECSRA response/URC.

Other real `AT+E*` tokens that look boot/config-related (found in the 256-token sweep,
none were in the task's guess list): `AT+EBOOT`, `AT+EFUN`, `AT+EDSDA`,
`AT+ESLOTSINFO`, `AT+ESLOTSMAP`, `AT+ECFGGET`/`AT+ECFGSET`/`AT+ECFGRESET`,
`AT+ESRVSTATE`, `AT+ESCREENSTATE`, `AT+EMDVER`, `AT+EMDT`, `AT+EPOF`, `AT+ESIMPOWER`.
No token containing `IND`, `INIT`, `BOOT`(other than `EBOOT`), or `READY` other than
`EBOOT` itself was found near the top of the alphabetized `AT+E*` list.

## Surprises / corrections vs. the prior recon

- **`RmcAtciCommonRequestHandler`, `RmcAtciRequestHandler`, `RmcAtciSpecialRequestHandler`
  ("ATCI" = AT Command Interface) have essentially *no* embedded `AT+...` command
  literals** (one exception: the literal `"ATV0"` string inside
  `RmcAtciRequestHandler::handleOemHookAtciInternalRequest`). This makes sense on
  reflection: these classes are generic *relays* for arbitrary AT text supplied by
  userspace (OEM hook raw AT command) or the modem side, not senders of a fixed
  command set — so "0 AT commands found" for these three classes is a real finding,
  not a gap in the method.
- **`RmcHardwareConfigRequestHandler` and `RmcWpModemRequestHandler` also came back
  with 0 AT commands found** across all their exported methods. For
  `RmcHardwareConfigRequestHandler` specifically, one method name
  (`sendHardwareConfigUrc`) suggested it *builds and emits* a hardware-config URC
  object rather than sending/parsing an `AT+...` string — consistent with 0 hits.
  `RmcWpModemRequestHandler` needs a closer look in a follow-up pass; it may build
  commands by string concatenation/format rather than a single adrp-resolved literal.
- **`RmcOemRequestHandler::requestGetImei()` sends plain `AT+CGSN`**, not
  `AT+EGMR=...` as the task's example implied for the whole class — `AT+EGMR` *is*
  used elsewhere in the same class (`requestSN()` → `AT+EGMR=0,5`,
  `requestGetImeisv()` → `AT+EGMR=0,9`), so the class mixes standard 3GPP commands
  (`AT+CGSN`, `AT+CGMR` for baseband version) with the proprietary `AT+EGMR` family
  depending on which specific value is being read. Confirmed by hand-reading the full
  disassembly of `requestGetImei()`, `requestSN()`, `requestGetImeisv()` and
  `requestBasebandVersion()`.
- **`RmcCommSimRequestHandler::smlSendAtSingleLine(...)` is a single dispatcher that
  sends any of ~10 different `AT+ESMLRSU=...` variants** and checks matching
  `+ESMLRSU: ...` response prefixes for each — SIM-lock/network-lock related
  (ESMLRSU ≈ "SIM lock restore/update"). One function, ten command variants — a good
  example of why per-method "at_commands" lists sometimes have far more entries than
  a naive one-command-per-method model would predict.
- Several Tier-B classes (`RmcCallControlImsRequestHandler`, `RmcImsControlRequestHandler`,
  `RmcImsRttControlRequestHandler`, `RmcMobileWifiRequestHandler`) came back with a much
  higher high-confidence fraction than expected for a "lighter pass" tier, simply
  because most of their methods are small (<150 instructions) — the automated pass
  naturally produces high confidence there without extra manual effort.

## Files

- `at-map.json` (next to this file) - full structured result (all 39 handlers, 922 de-duplicated methods, per-method AT commands / URC-or-prefix strings / confidence / cited adrp+add/ldr evidence; boot-handshake section; handler-count correction note).
- `AT-MAP-NOTES.md` - this file.

Intermediate/working files kept alongside them in the same directory for
reproducibility (not part of the deliverable, but not binaries either — safe to keep
or delete): `_dynsyms_raw.txt` (demangled dynsym dump), `_text_disasm.txt` (full
`.text` disassembly, 30 MB), `_rodata.bin` (raw 574,444-byte copy of the `.rodata`
section only — **not** the executable), `_extracted.json` (raw per-method evidence
before confidence/classification pass), `extract.py` / `build_output.py` (the two
scripts that produced everything above).
