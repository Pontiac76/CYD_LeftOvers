# CYD_LeftOvers — Project Outline

## Purpose

CYD_LeftOvers turns a Cheap Yellow Display (CYD) into a kitchen leftovers display.

The goal is simple: make prepared food in the fridge visible, dated, and harder to ignore so people eat leftovers before creating more leftovers.

This is not a grocery tracker, audit log, nutrition app, or cloud service. Future "Kitchen Command Center" nonsense can wait until the spaghetti problem is solved.

## Core Concepts

### LOS — Left Over Screen

The normal always-on display shown on the CYD.

The LOS should:

- Run in portrait orientation.
- Show current date/time.
- Show available leftovers grouped by the date they were added.
- Show both the added date and age, e.g. `May 10 - 2d`.
- Display older leftovers first; newer leftovers appear later/end.
- Page through the list every ~10–15 seconds if everything cannot fit.
- Avoid long status text where possible.
- Use a compact status bar with generated icons/symbols.

Potential status indicators:

- Active edit session: blinking red/record-style dot.
- WiFi state: generated WiFi-style symbol with color state.
- NTP state: generated clock-style symbol with color state.
- Dirty/cached data: small visual indicator if RAM changes have not yet been written to LittleFS.

## Startup / POST Behavior

Startup should behave like a PC POST:

1. Initialize display, touch, storage, WiFi config, time, and app data.
2. Validate that the ducks are sufficiently in a row.
3. Start normal operation.

If things break after startup, the CYD should keep operating locally as much as possible.

### WiFi

WiFi credentials live in `/wifi.txt` on LittleFS.

- If `/wifi.txt` is missing, empty, or contains no usable SSID, enter WiFi setup QR mode.
- If `/wifi.txt` exists but the AP is unavailable, keep retrying in the background.
- Do not immediately force setup just because a known AP is temporarily down.
- A reboot causes the CYD to rescan/retry available APs.
- If the network is unavailable and the user tries to enter edit mode, show a clear local message advising reboot/watch startup and then call tech support, meaning Stephen.

Future source may support renegotiating among several household APs.

### WiFi Setup Mode

WiFi setup is separate from leftovers editing.

- Device starts an AP/captive portal.
- CYD displays QR instructions for joining/configuring WiFi.
- Successful setup writes `/wifi.txt` to LittleFS.
- WiFi setup remains part of startup/POST behavior, not the normal leftovers edit workflow.

### NTP

The CYD needs WiFi mostly so it can reach the configured NTP server.

- At POST, retry NTP until an answer is received.
- During LOS, track the last successful NTP sync.
- If several sync cycles are missed, roughly 3–4, show an LOS status alert.
- NTP server must be configurable.
- Stephen will likely use an internal household NTP server.
- This is not time-critical; date resolution within a day is fine.
- When adding leftovers, use whatever day the CYD currently thinks it is.

## Data Model

### Current Leftovers

Stored as pipe-separated values, not CSV:

```text
/leftovers.psv
YYYY-MM-DD|food name
YYYY-MM-DD|another food
```

Rules:

- Store full year internally so sorting works across New Year.
- UI may hide the year.
- Each item only needs:
  - added date
  - food name
- No unique ID field.
- No audit/history.
- Once deleted, the item is gone.
- Items are unique per day by case-insensitive canonical name.
- The same food may appear on different days.
- The same food may not appear twice on the same day.
- Sort by date, oldest first, then name.

### Known Foods / Quick Add

Stored separately as a simple list:

```text
/known_foods.txt
pizza
spaghetti
mac-n-cheese
```

Rules:

- No dates.
- Unique canonical names only.
- Exists only to avoid repeated phone typing/autocorrect stupidity.
- Not a category system.
- Not inventory.

## Food Name Handling

Food names should be canonicalized for storage and cleaned for display.

Input rules:

- Allow basic printable US-keyboard ASCII characters.
- Reject/control weird non-printable input.
- Trim leading/trailing whitespace.
- Collapse repeated spaces.
- Convert to lowercase for storage.
- Maximum stored length: 40 characters.
- Replace or strip pipe `|`; preferred replacement is `_` because pipe is the PSV delimiter.

Batch add rules:

- Process textarea input line-by-line.
- Ignore blank/invalid lines.
- Each line must contain at least 3 non-space printable characters.
- Invalid short lines are ignored and not written.
- De-duplicate during processing.

Display rules:

- Render canonical lowercase names in title/camel-ish case.
- First character uppercase.
- Character after a space uppercase.
- Character after a dash uppercase.
- Word wrapping may be used for multi-word entries if needed.
- Avoid mid-word wrapping where possible.
- Font size and layout can be tuned later with real kitchen content.

## Leftovers Edit Session

Physical access to the CYD grants edit access.

### Session Token

Use the term **session token**, not necessarily UUID.

- Touching LOS generates a random token and shows an edit QR.
- Token may be longer than a traditional UUID if firmware can comfortably handle it.
- If CYD token is empty, missing, expired, or invalid, no edits are allowed.
- Browser requests without a valid token are rejected.
- Invalid/expired browser sessions should show a message telling the user to rescan the QR.
- Token lifetime: up to 1 hour unless replaced/cancelled/logout.

The device may be exposed through pfSense/HAProxy/HTTPS so phones can edit after moving from LAN to cellular. Risk is acceptable because the exposed function edits a household leftover list, not firmware or sensitive data.

Still be sane:

- Sanitize all input.
- Bound request/body sizes.
- Escape all HTML output.
- Expose only narrow routes.
- No arbitrary file editing.
- No firmware/config endpoints through leftover edit mode.

### QR Behavior

- Touch LOS: generate token and show QR.
- QR remains visible for about 1 minute.
- If no phone opens it, CYD returns to LOS.
- Phone opening the valid QR URL immediately returns CYD to LOS.
- Token remains valid for editing until logout, expiry, or cancellation.
- Touching the CYD while QR/session is active cancels/invalidates the token and returns to LOS.

### Web UI Layout

Preferred phone UI:

- Landscape/wide screens: split-pane layout.
- Narrow screens: stacked responsive layout.

Left pane:

- Current leftovers.
- Delete checkboxes.
- Checked items are deleted when changes are applied.

Right pane:

- Add textarea.
- Quick-add buttons/list.
- `Save to quick add` checkbox.

Actions:

- `Apply Changes`
  - Processes checked deletes and line-by-line adds in one request.
  - Updates RAM.
  - Immediately refreshes CYD LOS.
  - Browser returns to updated list.
  - Shows transient success feedback, e.g. `Changes applied. Press Logout to end session.`
- `Logout`
  - Writes dirty RAM data to LittleFS.
  - Retires token.
  - Returns a small fortune-style completion message.

Quick-add behavior:

- Clicking a known food appends/populates a new line in the textarea.
- It does not submit by itself.
- Quick-add list only stores unique canonical names.

## Persistence Strategy

Minimize LittleFS writes.

- Load leftovers and known foods into RAM at boot.
- During edit sessions, changes live in RAM and mark data dirty.
- Do not write to LittleFS after every add/delete.
- Write dirty data when:
  - user presses `Logout`, or
  - token/session expires.
- If power dies mid-session, losing a recent meatball entry is acceptable.
- UI should indicate when changes are cached/in RAM.

## Config

Because LittleFS is not as casually editable as an SD card, the firmware needs a basic config system and web config editor.

### Config File

Use `/config.txt` on LittleFS for non-WiFi configuration.

Potential keys:

- public/base URL for QR generation
- NTP server
- timezone
- UI colors
- LOS page interval
- token lifetime
- QR timeout
- NTP stale thresholds
- other firmware variables added later

WiFi remains separate in `/wifi.txt` and handled by WiFi setup mode.

### Editable vs Non-Editable Config

Not every config line should be editable from the web UI.

- Firmware owns a whitelist of config keys allowed in the config editor.
- Config page only exposes whitelisted editable keys.
- `/config.txt` may contain additional firmware variables that are read and preserved but not editable.
- When saving config:
  - read existing config into an internal structure/array,
  - update only allowed editable keys,
  - preserve non-editable known/unknown lines where practical,
  - write the resulting config back to LittleFS.
- Order does not matter.
- Content does.

A known-good `config.txt` can be flashed to LittleFS for disaster recovery/reset. After development, the device should be self-managing.

### Config Mode Access

Config editing should require deliberate physical confirmation.

Possible flow:

1. User requests config mode.
2. CYD shows a config confirmation QR.
3. Config QR expires after about 15 seconds.
4. Phone scans config QR to enter config mode.
5. Browser displays whitelisted config form.
6. Save writes `/config.txt`.
7. CYD reboots to apply changes.

This is intentionally narrow. No Webmin for leftovers.

## Non-Goals

For initial scope, do not build:

- cloud backend
- external API dependency
- grocery list
- audit/history
- user accounts
- arbitrary file editor
- firmware upload UI
- SD-card dependency
- SQL/database nonsense
- full kitchen command center

## Source Reuse / Refactor Direction

This project started from CYD_CLOCK source. Reuse and refactor freely.

Useful existing pieces:

- TFT display setup
- QR drawing
- touch handling
- WiFi STA connection
- WiFi setup portal
- NTP scheduling
- LittleFS helpers
- brightness/photoresistor handling

Clock/calendar-specific parts can be removed or replaced:

- schedule display
- remote update polling
- system ID switching
- SD config mirroring unless explicitly useful
- landscape clock rendering

Long-term direction: gradually turn reusable CYD pieces into generic modules that can be shared across CYD projects. Do not over-architect before the leftovers app works.
