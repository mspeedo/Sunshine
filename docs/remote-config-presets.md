# Remote configuration presets (Sunshine fork)

The preset API is a small addition to the existing **authenticated Sunshine HTTPS web UI** (normally `https://host:47990`). It does not create another listener, disable TLS or authorize paired streaming clients as administrators.

## API

| Method | Path | Body | Result |
| --- | --- | --- | --- |
| GET | `/api/config-presets` | none | `{"status":true,"presets":["HDR","SDR"]}` |
| POST | `/api/config-presets` | `{"name":"HDR","overwrite":false}` | Snapshots current on-disk configuration |
| POST | `/api/config-presets/apply` | `{"name":"HDR"}` | Stages snapshot as active configuration |
| DELETE | `/api/config-presets` | `{"name":"HDR"}` | Removes snapshot |

Authentication and CSRF validation are the same as on `/api/config`. Names must consist of 1–48 ASCII letters, numbers, underscores or hyphens; a leading hyphen is refused. All operations are guarded by the same mutex as normal web UI configuration saves.

**Applying does not restart Sunshine.** A separate authenticated `POST /api/restart` uses the same `platf::restart()` implementation as Sunshine's tray icon. Callers must warn that any active stream can disconnect. This separation avoids restarting without the user's intent.

## Storage and recovery

Presets are full-text snapshots in `config-presets/<name>.conf`, alongside the active `sunshine.conf` file. They are host-local, so multiple Moonlight devices see the same names. The currently active configuration is backed up beside its original path with the suffix `.before-remote-preset.bak` before a preset is applied. Save operations refuse duplicate names unless overwrite is explicitly requested. No configuration is loaded into the running encoder until restart.

## Security and deployment

- Configure the existing `origin_web_ui_allowed` policy for the desired trusted network (typically `lan`). Do **not** expose port 47990 to the public Internet; use a private VPN if needed.
- A paired GameStream client is **not** automatically authorized for administration. The Sunshine web username/password is still required.
- Keep the config directory private: a snapshot can contain paths and other potentially sensitive options.
- The API refuses path traversal and symbolic-link preset files.
- The Moonlight companion uses Sunshine's paired TLS certificate for strict certificate pinning.
- This feature does not change the GameStream protocol or its pairing/authentication behavior.

## Verification

New `tests/unit/test_remote_presets.cpp` covers validation, save, list, restore, collision refusal, backup and deletion. Full Sunshine CI and a runtime paired-host test are still required before production deployment.
