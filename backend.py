#!/usr/bin/env python3
"""
Test backend for Envoy ring buffer cache — per-host edition.

Run TWO instances on different ports to simulate two independent upstream hosts:

  python3 backend.py --port 8080 --name host-a   # Terminal 2
  python3 backend.py --port 8081 --name host-b   # Terminal 3

Each instance has completely independent hit counters, mirroring the fact
that the Envoy ring buffer cache maintains a separate ring buffer per host.

Endpoints (same on both instances):
  GET  /resource/<id>       Plain cacheable resource (no Cache-Control)
  GET  /slow/<id>           2-second delay — for coalescing tests
  GET  /evict/<id>          Eviction-test resource (use ids: a, b, c)
  GET  /cc/<id>             Has Cache-Control: max-age=60 (unsupported by cache)
  GET  /vary/<id>           Has Vary: User-Agent (unsupported by cache)
  GET  /status              Hit counters for this instance
  POST /reset               Reset hit counters on this instance

Usage:
  pip install flask
  python3 backend.py --port 8080 --name host-a
  python3 backend.py --port 8081 --name host-b
"""

import argparse
import json
import threading
import time
from datetime import datetime, timezone
from flask import Flask, request, Response, jsonify

# ---------------------------------------------------------------------------
# Globals (per-process — each instance has its own)
# ---------------------------------------------------------------------------
_lock = threading.Lock()
_counters: dict[str, int] = {}   # resource_key -> backend hit count
_instance_name = "unknown"


def _count(key: str) -> int:
    with _lock:
        _counters[key] = _counters.get(key, 0) + 1
        return _counters[key]


def _json_response(resource_key: str, note: str, extra_headers: dict | None = None, delay: float = 0) -> Response:
    if delay:
        time.sleep(delay)

    now = datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S GMT")
    hit = _count(resource_key)

    body = json.dumps({
        "instance": _instance_name,
        "resource_key": resource_key,
        "backend_hit_count": hit,
        "served_at": now,
        "note": note,
    }, indent=2) + "\n"

    resp = Response(body, status=200, mimetype="application/json")
    resp.headers["X-Backend-Instance"] = _instance_name
    resp.headers["X-Backend-Hit"]      = str(hit)
    resp.headers["X-Served-At"]        = now

    if extra_headers:
        for k, v in extra_headers.items():
            resp.headers[k] = v

    return resp


# ---------------------------------------------------------------------------
app = Flask(__name__)
# ---------------------------------------------------------------------------


@app.route("/resource/<rid>")
def resource(rid):
    """Plain cacheable resource. No Cache-Control."""
    return _json_response(
        f"resource:{rid}",
        note="No Cache-Control. Ring buffer decides caching entirely.",
    )


@app.route("/slow/<rid>")
def slow(rid):
    """2-second delayed resource for request-coalescing tests."""
    return _json_response(
        f"slow:{rid}",
        note="Delayed 2 s. Concurrent requests should coalesce into one upstream call.",
        delay=2,
    )


@app.route("/evict/<rid>")
def evict(rid):
    """
    Eviction test resource.
    With max_entries_per_host=2, filling slots with a, b then adding c
    should evict a (oldest). Each HOST has its own ring buffer — so a full
    ring on host-a does NOT affect the ring on host-b.
    """
    return _json_response(
        f"evict:{rid}",
        note=(
            f"Ring buffer entry '{rid}' on {_instance_name}. "
            "Inserting a 3rd entry evicts the oldest in THIS host's ring only."
        ),
    )


@app.route("/cc/<rid>")
def cache_control(rid):
    """Resource with Cache-Control: max-age=60. Cache ignores this directive."""
    return _json_response(
        f"cc:{rid}",
        note="Cache-Control: max-age=60 present. The ring buffer cache should ignore it.",
        extra_headers={
            "Cache-Control": "max-age=60, public",
            "ETag": f'"{rid}-etag-v1"',
        },
    )


@app.route("/vary/<rid>")
def vary(rid):
    """Resource with Vary: User-Agent. Cache ignores Vary."""
    ua = request.headers.get("User-Agent", "unknown")
    return _json_response(
        f"vary:{rid}",
        note=f"Vary: User-Agent present (your UA: {ua!r}). Cache should ignore Vary.",
        extra_headers={"Vary": "User-Agent"},
    )


@app.route("/status")
def status():
    with _lock:
        snap = dict(_counters)
    return jsonify({
        "instance": _instance_name,
        "backend_hit_counters": snap,
        "total_hits": sum(snap.values()),
    })


@app.route("/reset", methods=["POST"])
def reset():
    with _lock:
        _counters.clear()
    return jsonify({"instance": _instance_name, "status": "reset"})


# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Ring-buffer cache test backend")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--name", type=str, default="backend")
    args = parser.parse_args()

    global _instance_name
    _instance_name = args.name

    print("=" * 60)
    print(f"  Ring Buffer Cache Test Backend — {args.name}")
    print(f"  Listening on http://127.0.0.1:{args.port}")
    print("=" * 60)
    app.run(host="127.0.0.1", port=args.port, threaded=True)


if __name__ == "__main__":
    main()