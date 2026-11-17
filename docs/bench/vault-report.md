# Vault core and functional UI verification

Date: 2026-10-18. Scope: FW-22 core and FW-23 core/UI integration. USB CDC/HID transport and the CLI are maintained by the root/BE owners. This report does not claim FW-23 hardware acceptance.

| Acceptance item | Status / evidence |
|---|---|
| FW-22 T7, RFC 6238 | Passed: all six Appendix B times for SHA1/SHA256/SHA512, 18 vectors. |
| FW-22 T7, per-entry crypto | Passed: PBKDF2/HKDF-derived key, AES-256-GCM roundtrip, wrong entry-id AAD rejected and plaintext zeroed. |
| FW-22 T7, backup roundtrip | Passed: two typed entries, Chinese/surrogate-pair name and escaped note, JSON serialization, NVB1 encrypt/decrypt, profile/tag tampering rejected. |
| C11/Xtensa compile | Passed: vault.c, vault_crypto.c, vault_json.c, vault_otpauth.c, vault_app.c, vault_view.c using actual IDF project compile flags with `-Wall -Wextra -Werror -fsyntax-only`; whole-image link is the FW owner's check. |
| Internal RAM / zeroization review | Passed source review: keys/entry strings/JSON/tokens use explicit INTERNAL heap or DRAM static storage; PIN worker uses an internal static stack; mbedTLS internal-only allocation is required at compile time. JSON growth copies to a fresh internal allocation and zeroes the previous buffer. Sensitive LVGL strings use static pointers to internal buffers. This is source evidence, not a heap trace on hardware. |
| PIN benchmark / persistence | Implemented, hardware measurement pending: startup estimates iterations for 300 ms; a configured Vault keeps its persisted iterations until a PIN change. |
| Anti-bruteforce | Implemented, power-cycle acceptance pending: NVS commits the increment before verification. Five failures persist a cooldown flag; each boot with the flag restarts 300 s, so reset cannot bypass it. Ten/twenty/off policies persist. Erase affects only the independent Vault partition, then regenerates the software root. |
| eFuse mode | Read-only existing HMAC_UP-slot path implemented. No burn API exists in this component. Not executed; no eFuse authorization was requested or assumed. |
| Import / export | Implemented: unlocked + two-minute local management gate, fixed NVB1 v1 header, GCM authentication before JSON, duplicate `(type,name)` comparison, preview then atomic append commit, cancel/timeout zeroization. Declared input length is bounded by the actual 64 KiB Vault partition. USB/CLI roundtrip on the device is pending. |
| Functional view | Implemented: touch-only 6–8 digit PIN setup/repeat/unlock, background PIN derivation, entry selection and TOTP/countdown, 40 px mono list code and enlarged detail, hidden password/five-second reveal, generator/options/save, USB hold confirmation, and thirty-second away lock. Visual/hardware acceptance is pending. |
| FW-23 three-platform HID | Not run: Windows/macOS/Linux text-editor checks require the integrated device; no synthetic compile result is counted as keyboard evidence. |
| SPDX | Passed: repository check observed 223 sources, zero failures. Bundled jsmn retains its MIT license and is excluded from project-owned SPDX replacement. |
| Personal-value scan | Vault sources and both guides passed. At this check the repository had one unrelated NVIDIA query-field false positive in collectors.py; root/BE were informed. |

T7 used the existing ESP-IDF mbedTLS source and Unity on Linux under WSL, without HTTP or physical device access:

```sh
cmake -S firmware/components/vault/test -B build/vault-t7 -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/vault-t7 -j 6
ctest --test-dir build/vault-t7 --output-on-failure
```

Result: `3 Tests 0 Failures 0 Ignored`; CTest `1/1` passed. These are the three categories authorized under T7; no additional test suite was introduced.

Concurrency: public operations use an internal recursive mutex. UI/HID readers hold `vault_guard_enter/leave` while borrowing entry pointers. No LVGL call occurs in the crypto/NVS component. The local PIN worker returns its result through a queue; USB crypto executes on the USB task. Password buffers and full entry storage are zeroed on lock, cancellation or transfer completion as appropriate. A generated password remains internal while unlocked so it can be saved or typed; only its five-second display authorization expires.

Remaining integration responsibilities: FW must call `vault_init` with real RNG entropy enabled, process `vault_tick` errors in the service loop, invoke `vault_app_tick` from the LVGL task, and lock on screen-off/Key3. Root owns confirmation overlays and CDC/HID. Verify reset during wrong-PIN verification, cooldown across reset, both erase thresholds/off, corrupt/incorrect-passphrase import leaving the previous library unchanged, export→erase→import, and three-platform typing before closing FW-23. No new tests are requested; use PLAN appendix B/C manual scenarios.

Security limits remain those in SPEC §9.3: default software mode permits offline PIN guessing after Flash extraction; no Secure Boot, Flash encryption, probe or side-channel protection is claimed. All example entries are synthetic. No real credentials were used, no hardware erase was performed by this agent, and no eFuse was programmed.
