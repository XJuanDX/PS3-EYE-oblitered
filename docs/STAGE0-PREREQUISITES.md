# Stage 0 — Prerequisites checklist

**Owner: the user. The agent must not run any of this.**

Every command below needs an **elevated** PowerShell/CMD prompt
("Run as administrator"). The current session runs as an ordinary
non-elevated local user, so nothing here can be executed by the agent by
design.

Work top to bottom. Each step has its own verification and its own rollback, so
a failure does not leave you stranded.

---

## Why these three things

| Requirement | Needed for | If missing |
|---|---|---|
| WDK 10.0.19041 | compiling the AVStream minidriver (`km` headers, `stampinf`, `inf2cat`, `signtool`) | stage 2 cannot build at all |
| Secure Boot **off** | Windows refuses `testsigning on` while Secure Boot is enabled | local test driver will not load |
| `testsigning on` | Win10 x64 refuses to load a self-signed kernel driver | driver installs but fails to start (`Code 52`) |

Stage 1 needs **none** of this. Do not block stage 1 on stage 0.

---

## Step 1 — Install WDK 10.0.19041

WDK 10.0.19041 corresponds to **Windows 10, version 2004**. It is the newest WDK
that pairs with Visual Studio 2019 Build Tools 16.11, which is what this machine
has.

1. Download `wdksetup.exe` from the Microsoft page **"Download the Windows
   Driver Kit"** → *Other WDK downloads* → *Windows 10, version 2004*.
   Do not use a third-party mirror. After downloading, verify the installer is
   genuinely Microsoft-signed:

   ```powershell
   Get-AuthenticodeSignature .\wdksetup.exe | Format-List Status,SignerCertificate
   ```

   `Status` must be `Valid` and the signer must be `Microsoft Corporation`.

2. Run `wdksetup.exe`. On the *Select Features* page keep the default
   **Windows Driver Kit** feature set.

3. When the installer asks for a Visual Studio instance, it should discover
   `C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools`.
   If it does **not** list Build Tools, stop here and report: that is the point
   where VS 2019 Community (same 16.11 minor version, "Desktop development with
   C++" workload) becomes mandatory. Do not force the installer.

4. Verification:

   ```powershell
   Test-Path "C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\km"
   Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64" -Include stampinf.exe,inf2cat.exe,signtool.exe -Recurse
   ```

   Expected: `True`, then three file paths.

5. Rollback: *Settings → Apps → Windows Software Development Kit* →
   uninstall. The SDK component is harmless to leave installed.

---

## Step 2 — Disable Secure Boot

Currently `UEFISecureBootEnabled = 1`. This is the only step that cannot be
done from Windows.

1. Reboot into UEFI/BIOS setup. From Windows:

   ```powershell
   shutdown /r /fw /t 0
   ```

   (`/fw` requires elevation and firmware support. If it errors, use the F2 /
   Del / F10 key for your board during POST.)

2. In setup, find **Secure Boot** (usually under *Security*, *Boot* or
   *Authentication*) and set **Secure Boot → Disabled**.
   The option is only editable while *Setup Mode* is available; if it is greyed
   out, first clear the platform keys / "Restore Factory Keys" as prompted by
   your board. Exact wording differs per vendor — this is the one place the
   checklist cannot be literal.

3. Save and exit, boot Windows, then verify:

   ```powershell
   (Get-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State).UEFISecureBootEnabled
   try { Confirm-SecureBootUEFI } catch { "not supported / disabled" }
   ```

   Expected: `0`, and `False` or `not supported / disabled`.

4. Rollback: re-enable Secure Boot in the same menu. Note that once the test
   driver is installed, Windows will refuse to boot it with Secure Boot on —
   uninstall the driver first (`docs/UNINSTALL.md`, written in stage 4).

---

## Step 3 — Enable test signing

Only works after step 2.

1. Elevated prompt:

   ```powershell
   bcdedit /set testsigning on
   ```

   Success prints *The operation completed successfully.*

2. Reboot.

   ```powershell
   shutdown /r /t 0
   ```

3. Verification:

   ```powershell
   bcdedit /enum "{current}" | Select-String -Pattern "testsigning|nointegritychecks"
   ```

   Expected: a `testsigning         Yes` line. The desktop will also show a
   **Test Mode** watermark in the lower-right corner; that is normal and
   expected.

4. Optional but recommended, to remove one more variable: *Windows Security →
   Device security → Core isolation details → Memory integrity* → **Off**, then
   reboot. Memory integrity (HVCI) can reject otherwise validly test-signed
   drivers.

5. Rollback:

   ```powershell
   bcdedit /deletevalue testsigning
   ```

   then reboot.

---

## Step 4 — Create the test signing certificate

Required before stage 2. Needs elevation, do it once. Replace nothing — the
subject string is opaque, keep it as-is so the build scripts match.

```powershell
$cert = New-SelfSignedCertificate `
  -Type Custom `
  -Subject "CN=PS3Eye Driver Test" `
  -KeyUsage DigitalSignature `
  -FriendlyName "PS3Eye Driver Test" `
  -CertStoreLocation "Cert:\LocalMachine\My" `
  -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

$cert.Thumbprint
```

`2.5.29.37 … 1.3.6.1.5.5.7.3.3` is the Code Signing EKU. Without it
`signtool` will silently produce a signature the kernel ignores.

Export and trust it (same elevated session):

```powershell
$pwd = Read-Host -AsSecureString "PFX password"
Export-PfxCertificate -Cert $cert -FilePath "$env:USERPROFILE\Desktop\ps3eye-test.pfx" -Password $pwd
Export-Certificate  -Cert $cert -FilePath "$env:USERPROFILE\Desktop\ps3eye-test.cer"

Import-Certificate -FilePath "$env:USERPROFILE\Desktop\ps3eye-test.cer" -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath "$env:USERPROFILE\Desktop\ps3eye-test.cer" -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

Verification:

```powershell
Get-ChildItem Cert:\LocalMachine\Root           | Where-Object Subject -like "*PS3Eye*" | Format-List Subject,Thumbprint
Get-ChildItem Cert:\LocalMachine\TrustedPublisher | Where-Object Subject -like "*PS3Eye*" | Format-List Subject,Thumbprint
```

Expected: the same thumbprint in all three stores (My, Root, TrustedPublisher).

Keep `ps3eye-test.pfx` out of the repository — it is the private key.

Rollback:

```powershell
Get-ChildItem Cert:\LocalMachine\My, Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher |
  Where-Object Subject -like "*PS3Eye Driver Test*" | Remove-Item
```

---

## Step 5 — Report back

Paste the output of these four lines so stage 2 can start:

```powershell
Test-Path "C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\km"
(Get-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State).UEFISecureBootEnabled
bcdedit /enum "{current}" | Select-String testsigning
Get-ChildItem Cert:\LocalMachine\TrustedPublisher | Where-Object Subject -like "*PS3Eye*" | Select-Object -ExpandProperty Thumbprint
```

---

## Order of operations that minimises risk

1. Stage 1 now (no prerequisites, no admin, fully reversible).
2. Confirm the camera produces real 640x480 frames from user-mode.
3. Only then do steps 1-4 above.
4. Stage 2 with the synthetic pattern.
5. Stage 3 to connect the two.

Rationale: if the capture core cannot be made to work, the kernel driver is
pointless and the Secure Boot change would have been for nothing.
