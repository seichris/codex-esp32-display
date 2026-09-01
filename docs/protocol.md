# Device protocol v1

## Request

```http
GET /api/v1/attention HTTP/1.1
Authorization: Bearer <bridge-token>
Accept: application/json
```

The response is `Cache-Control: no-store`. A missing or incorrect token returns
HTTP 401.

## Response

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

## Fields consumed by firmware

The ESP32 deliberately consumes only a bounded subset:

- top-level `totalCount`, `truncated`, and diagnostics;
- item `title`, `project`, `status`, `ageSeconds`, `unread`, `pinned`, and
  `newResult`.

This allows the host API to add fields without requiring a firmware update.

## Status values

- `idle`
- `running`
- `waiting_input`
- `waiting_approval`
- `error`

Running or error alone does not cause inclusion. Such a status appears when the
same thread is also unread or pinned.

## Versioning

Firmware rejects responses whose `version` is not `1`. Additive fields are
allowed within v1. Removing or changing the meaning/type of a field requires a
new protocol version.
