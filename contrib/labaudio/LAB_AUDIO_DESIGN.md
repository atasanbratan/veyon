# Lab Audio for Veyon — design

Goal: live classroom audio in Veyon on Windows — the teacher speaks to students' headsets, talks two-way with one
student, or listens to a student's microphone. Built as a Veyon feature plugin (`plugins/labaudio`) on branch
`labaudio` of the Veyon source (upstream 4.11.x, commit 1cebf45), shipped as our own Veyon build/installer.

## Teacher features (Veyon Master toolbar, group "Lab audio")

| Feature | Who hears whom | Selection |
|---|---|---|
| Speak to students | Teacher → selected (or all) students | any |
| Talk with student | Teacher ↔ one student | exactly one |
| Listen to student | Student → teacher | exactly one |
| Group work | Students ↔ their group (pairs or 2–6); teacher listens to or joins one group | any (≥ 2) |

Starting another mode on a computer replaces its previous mode. "Stop" on the group stops everything.

### Classroom controls

- **Call the teacher at any time**: a tray icon (raised hand) on every student PC, started by the Veyon Server as
  soon as a Veyon Master is connected and a user is logged in; the ✋ button on the audio notice does the same.
  The state lives in the student's Veyon Server (`LabAudioCallBoard`), travels to every connected Master through
  Veyon's own connection (`sendAsyncFeatureMessages`, no extra port) and back to the tray and the notice. A call
  waits until a Master connects. Master shows "Students calling" (name, computer, group, time, beep) with **Done**,
  **All done** and **Students may call the teacher** (off e.g. during a test; remembered per teacher). Also opened
  from the toolbar: Lab audio → Student calls.
- **Student volume**: slider on the notice, 0–200 %, applied in the mixer, remembered per Windows user.
- **Teacher microphone**: the teacher panel (Speak / Talk) and the group panel have **Microphone on/off** and
  **Push to talk** (hold the button or Space while the panel is active). Muting sends silent frames so the
  students' jitter buffers do not treat the pause as network jitter.

### Recording

- **● Record** in the teacher panel (Speak, Talk, Listen) records the conversation: what the teacher hears plus the
  teacher's microphone, mixed into one file. **● Record each group** in the group panel records every group into its
  own file (members plus the teacher while talking with that group), e.g. for oral exams.
- Files: `Documents\Lab audio recordings\<date time> <label>.opus` on the teacher PC (label e.g. "Talk with Anna",
  "Group 2 - Carla, Dan"); Ogg Opus, 32 kbit/s mono (≈ 2.4 MB per 10 minutes). **Recordings…** opens the folder.
  Another student or new groups start a new file.
- The hub mixes the decrypted streams with the same jitter-buffered mixer as playback, driven by the wall clock (one
  20 ms frame per 20 ms), and writes the Ogg pages itself (`LabAudioOggOpusWriter`, RFC 3533 / RFC 7845, one page
  per second, so a file stays playable even if the hub is killed).
- Students always see "● This conversation is being recorded" (`Recording` status flag). Recording never carries
  over into the next mode.

### Group work

A dialog asks for the group size (2–6, default pairs) and whether to mix students randomly; a single student left over
joins the last group. A teacher panel next to Veyon Master lists the groups with **Listen** and **Talk** buttons and a
**New groups** button (same size, new mix). Group work ends with the toolbar button (Veyon stops every mode before it
starts another, so joining a group cannot be a separate toolbar mode).

- Each group gets its own random token; the hub maps token → group, so a student can only join their own group.
- Student audio travels through the hub, which forwards the Opus packets unchanged (`PeerAudio`, with the source id) to
  the other members; each student mixes the streams. No transcoding on the teacher PC; a group of n uses
  n·(n−1) × 4 kB/s.
- The teacher hears the focus group and, with **Talk**, the group hears the teacher. Students always see
  "The teacher is listening to your group" / "The teacher has joined your group" (`Status` frame).
- Regrouping sends new tokens: the hub disconnects old group members, who reconnect with their new token.

## Components

```
Veyon Master (teacher)                    Veyon Server (each PC)            Worker processes
  LabAudio feature ──feature msg──▶  teacher PC server ──▶ LabAudioHub (teacher's session, runs as user)
                    ──feature msg──▶  student PC server ──▶ LabAudioClient (student's session, runs as user)
                                                              │
                         TCP :11500(+session id) ◀────────────┘  Opus audio frames
```

- Control messages use Veyon's existing authenticated, encrypted Master↔Server channel (same as Demo mode).
- **Hub** (`LabAudioHub`): `QTcpServer` in an unmanaged session worker on the teacher PC. Captures the teacher's default
  microphone only while a client needs it; plays/mixes students' microphones only while a client sends.
- **Client** (`LabAudioClient`): unmanaged session worker on each student PC; connects to the master computer's address
  (taken from the peer address of the control connection), reconnects every 2 s, shows a non-closable notice
  ("The teacher is speaking" / "The teacher can hear your microphone" + level meter) while active.
- Workers run **as the logged-in user** so Windows' default headset and microphone privacy settings apply.

## Audio

- Opus, 48 kHz mono, 20 ms frames, 32 kbit/s VOIP mode, in-band FEC. ≈ 4 kB/s per stream; 30 students listening to a
  broadcast ≈ 1 Mbit/s from the teacher PC.
- Capture/playback via Qt Multimedia (WASAPI). Any device format is converted (downmix + linear resampling).
- Receive side: per-stream jitter buffer, starts at 60 ms, adapts between 60 and 300 ms (grows after each underflow, shrinks after 15 s without one,
  excess latency is drained by skipping 1 sample in 200), Opus packet-loss concealment
  for up to two missing frames, underflow → rebuffer.
- Target end-to-end latency on a wired LAN: < 150 ms (to be measured).

## Wire protocol (`LabAudioProtocol.h`)

`[u32 length][u8 type][payload]` over TCP (Nagle disabled). Types: Hello (version, key id, client nonce), HelloAck (accepted, hub nonce), Auth (roles),
Audio (u32 sequence + Opus packet), Bye, Status (u8 teacher and recording flags), PeerAudio (u32 source id + Audio
payload); calls travel through Veyon, not this connection. Max payload 8 KiB; Hello and Auth must arrive within 5 s. From the Auth frame on, every frame is `Encrypted`.

## Security

- Each activation uses a random 512-bit token generated by the Master (`CryptoCore::generateChallenge`) and delivered
  only through Veyon's authenticated, encrypted channel; each group in group work has its own token. A new token is
  generated when lab audio stops, so old clients cannot reconnect.
- **The token never travels over the audio connection** (protocol v2). Hello carries a public key id (truncated
  SHA-256) and a 16-byte client nonce; HelloAck a 16-byte hub nonce. Both sides derive one AES-256 key per direction
  with HKDF-SHA256(token, client nonce || hub nonce). The client's first encrypted frame (Auth, carrying its roles)
  proves it knows the token; until then the hub sends it nothing but the HelloAck.
- **All audio and control frames are AES-256-GCM** (OpenSSL) with an implicit per-direction counter as nonce: a
  modified, replayed, reordered or injected frame fails authentication and closes the connection. A forger who sniffed
  a key id cannot authenticate; frames of one connection are useless in another. Overhead 17 bytes per frame.
  The hub decrypts student audio and re-encrypts it per listener for group work.
- Not provided: forward secrecy within one activation (whoever obtains that activation's token from the teacher's
  Veyon Master memory could decrypt recorded traffic of that activation).
- Hub accepts ≤ 100 clients, drops clients that fail to authenticate or send malformed frames, and does not queue more
  than 64 KiB per client.
- Firewall: the hub runs inside `veyon-worker.exe`; the installer adds the inbound rule "Veyon Lab Audio" (TCP 11500–11599,
  all profiles) and removes it on uninstall.

## Wait list

- Configuration page (port is fixed at 11500 + session id, audio quality).
- Translations.
