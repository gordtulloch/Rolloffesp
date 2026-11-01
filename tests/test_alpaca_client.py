import contextlib
import io
import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

from tools.alpaca_client import AlpacaDomeClient, AlpacaError, main, parse_shutter_state


class AlpacaTestServer(ThreadingHTTPServer):
    requests = []
    response_overrides = {}


class AlpacaTestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        AlpacaTestServer.requests.append(("GET", self.path, {}))
        parts = urlsplit(self.path)
        value = {"connected": True, "shutterstatus": 0}.get(
            parts.path.rsplit("/", 1)[-1]
        )
        self.respond({"Value": value, "ErrorNumber": 0, "ErrorMessage": ""})

    def do_PUT(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode()
        fields = parse_qs(body)
        AlpacaTestServer.requests.append(("PUT", self.path, fields))
        self.respond(
            AlpacaTestServer.response_overrides.get(
                self.path,
                {"Value": None, "ErrorNumber": 0, "ErrorMessage": ""},
            )
        )

    def respond(self, payload):
        data = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, _format, *_args):
        pass


class AlpacaClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        AlpacaTestServer.requests = []
        AlpacaTestServer.response_overrides = {}
        cls.server = AlpacaTestServer(("127.0.0.1", 0), AlpacaTestHandler)
        cls.server_thread = threading.Thread(target=cls.server.serve_forever)
        cls.server_thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.server_thread.join()

    def setUp(self):
        AlpacaTestServer.requests = []
        AlpacaTestServer.response_overrides = {}

    def test_status_reads_connection_and_shutter_state(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = main(["--base-url", self.base_url, "status"])

        self.assertEqual(result, 0)
        self.assertIn("Connected: True", output.getvalue())
        self.assertIn("Shutter status: Open (0)", output.getvalue())
        self.assertEqual(
            [urlsplit(request[1]).path.rsplit("/", 1)[-1] for request in AlpacaTestServer.requests],
            ["connected", "shutterstatus"],
        )
        self.assertIn("ClientID=1", AlpacaTestServer.requests[0][1])
        self.assertIn("ClientTransactionID=1", AlpacaTestServer.requests[0][1])

    def test_open_sends_standard_alpaca_put(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = main(["--base-url", self.base_url, "open"])

        method, path, fields = AlpacaTestServer.requests[0]
        self.assertEqual(result, 0)
        self.assertEqual(method, "PUT")
        self.assertTrue(path.endswith("/api/v1/dome/0/openshutter"))
        self.assertEqual(fields["ClientID"], ["1"])
        self.assertEqual(fields["ClientTransactionID"], ["1"])

    def test_connect_sets_connected_true(self):
        with contextlib.redirect_stdout(io.StringIO()):
            result = main(["--base-url", self.base_url, "connect"])

        self.assertEqual(result, 0)
        self.assertEqual(AlpacaTestServer.requests[0][2]["Connected"], ["true"])

    def test_alpaca_errors_are_reported(self):
        AlpacaTestServer.response_overrides[
            "/api/v1/dome/0/abortslew"
        ] = {
            "Value": None,
            "ErrorNumber": 1025,
            "ErrorMessage": "Not connected",
        }
        client = AlpacaDomeClient(self.base_url, 0, 1, 1.0)

        with self.assertRaisesRegex(AlpacaError, "Not connected"):
            client.put("abortslew")

    def test_shutter_state_rejects_unknown_enum(self):
        with self.assertRaisesRegex(AlpacaError, "Unknown ShutterStatus"):
            parse_shutter_state(99)


if __name__ == "__main__":
    unittest.main()
