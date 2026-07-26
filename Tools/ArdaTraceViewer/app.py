"""Local Flask host for the ArdaTrace offline viewer."""

from __future__ import annotations

import argparse
import threading
from pathlib import Path
from typing import Optional

from flask import Flask, jsonify, render_template, request
from werkzeug.exceptions import RequestEntityTooLarge

from trace_reader import TraceFormatError, parse_bytes, parse_file


MAX_UPLOAD_BYTES = 256 * 1024 * 1024


def create_app(initial_trace: Optional[str] = None) -> Flask:
    app = Flask(__name__)
    app.config["MAX_CONTENT_LENGTH"] = MAX_UPLOAD_BYTES
    app.config["SESSION_DATA"] = None
    app.config["SESSION_FILENAME"] = None
    session_lock = threading.Lock()

    if initial_trace:
        path = Path(initial_trace).expanduser()
        app.config["SESSION_DATA"] = parse_file(path)
        app.config["SESSION_FILENAME"] = path.name

    @app.get("/")
    def index():
        return render_template("index.html")

    @app.get("/api/session")
    def get_session():
        with session_lock:
            session = app.config["SESSION_DATA"]
            filename = app.config["SESSION_FILENAME"]
        if session is None:
            return jsonify({"error": "No trace is open"}), 404
        return jsonify(
            {
                "filename": filename,
                "session": session,
            }
        )

    @app.post("/api/session")
    def upload_session():
        upload = request.files.get("trace")
        if upload is None or not upload.filename:
            return jsonify({"error": "Choose a trace file to upload"}), 400
        try:
            session = parse_bytes(upload.read())
        except TraceFormatError as exc:
            return jsonify({"error": str(exc)}), 400
        with session_lock:
            app.config["SESSION_DATA"] = session
            app.config["SESSION_FILENAME"] = Path(upload.filename).name
            filename = app.config["SESSION_FILENAME"]
        return jsonify(
            {
                "filename": filename,
                "session": session,
            }
        )

    @app.errorhandler(RequestEntityTooLarge)
    def upload_too_large(_error):
        return jsonify({"error": "Trace exceeds the 256 MiB upload limit"}), 413

    return app


def main() -> None:
    parser = argparse.ArgumentParser(description="View an ArdaTrace capture locally")
    parser.add_argument("trace", nargs="?", help="trace file to open at startup")
    parser.add_argument("--host", default="127.0.0.1", help="address to bind")
    parser.add_argument("--port", type=int, default=5000, help="port to bind")
    parser.add_argument(
        "--debug", action="store_true", help="enable Flask debug mode explicitly"
    )
    args = parser.parse_args()

    try:
        app = create_app(args.trace)
    except (OSError, TraceFormatError) as exc:
        parser.error(str(exc))
    app.run(host=args.host, port=args.port, debug=args.debug)


if __name__ == "__main__":
    main()
