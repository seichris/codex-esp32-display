# Device protocol v1

All device data endpoints require:

```http
Authorization: Bearer <bridge-token>
Accept: application/json
```

Responses are `Cache-Control: no-store`. A missing or incorrect token returns
HTTP 401.

## Attention list

```http
GET /api/v1/attention
```

```json
{
  "version": 1,
  "generatedAt": "2026-09-01T00:00:00.000Z",
  "count": 3,
  "totalCount": 5,
  "truncated": true,
  "items": [
    {
      "id": "opaque-thread-id",
      "title": "Implement cadence smoothing",
      "preview": "Please implement cadence smoothing…",
      "project": "open-bike-computer",
      "cwd": "/Users/chris/open-bike-computer",
      "status": "waiting_input",
      "unread": true,
      "pinned": true,
      "newResult": false,
      "updatedAt": 1788220800,
      "ageSeconds": 120,
      "reasons": ["waiting_input", "unread", "pinned"]
    }
  ],
  "diagnostics": {
    "desktopStateAvailable": true,
    "sourceError": null
  }
}
```

Firmware consumes the ID, title, project, status, age, unread, pinned, and new
flags, plus top-level count/truncation/diagnostics.

Status values: `idle`, `running`, `waiting_input`, `waiting_approval`, `error`.
Running or error alone does not cause inclusion.

## Latest thread text

```http
GET /api/v1/threads/<url-encoded-thread-id>/latest
```

Only a thread present in the current bounded attention payload may be read. A
thread outside that list returns HTTP 404.

```json
{
  "version": 1,
  "id": "opaque-thread-id",
  "title": "Implement cadence smoothing",
  "project": "open-bike-computer",
  "status": "waiting_input",
  "reasons": ["waiting_input", "unread"],
  "updatedAt": 1788220800,
  "generatedAt": "2026-09-01T00:00:01.000Z",
  "kind": "agent",
  "text": "The implementation is complete…",
  "truncated": false
}
```

`kind` is one of `agent`, `plan`, `user`, `preview`, or `empty`. Text is capped
at 5,600 UTF-8 bytes by the bridge.

## Versioning

Firmware rejects responses whose `version` is not `1`. Additive fields are
allowed within v1. Removing or changing a field's meaning/type requires a new
protocol version.
