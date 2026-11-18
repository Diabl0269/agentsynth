#!/usr/bin/env python3
"""A stand-in Ollama server for scenarios that need the AI to stay busy: it lists one model and then never answers a chat.

    python3 Tools/ui-verify/fixtures/ollama_stall_stub.py [port]     # default 18935; stop it with Ctrl+C
"""

import http.server
import json
import socketserver
import sys
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = json.dumps({"models": [{"name": "stub:latest", "model": "stub:latest"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        time.sleep(300)

    def log_message(self, *args):
        pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


if __name__ == "__main__":
    Server(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 18935), Handler).serve_forever()
