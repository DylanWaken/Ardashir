import io
import unittest

from app import MAX_UPLOAD_BYTES, create_app
from test_trace_reader import valid_capture


class TraceViewerAppTests(unittest.TestCase):
    def setUp(self):
        self.app = create_app()
        self.app.config["TESTING"] = True
        self.client = self.app.test_client()

    def test_index_loads_without_session(self):
        response = self.client.get("/")
        self.assertEqual(response.status_code, 200)
        self.assertIn(b"Trace Viewer", response.data)
        self.assertNotIn(b"<script src=\"http", response.data)

    def test_session_is_404_before_opening_capture(self):
        response = self.client.get("/api/session")
        self.assertEqual(response.status_code, 404)
        self.assertEqual(response.get_json()["error"], "No trace is open")

    def test_upload_and_retrieve_session(self):
        response = self.client.post(
            "/api/session",
            data={
                "trace": (
                    io.BytesIO(valid_capture(name="<script>alert(1)</script>")),
                    "../../unsafe.trace",
                )
            },
            content_type="multipart/form-data",
        )
        self.assertEqual(response.status_code, 200)
        payload = response.get_json()
        self.assertEqual(payload["filename"], "unsafe.trace")
        self.assertEqual(
            payload["session"]["names"][0]["label"], "<script>alert(1)</script>"
        )

        retrieved = self.client.get("/api/session")
        self.assertEqual(retrieved.status_code, 200)
        self.assertEqual(retrieved.get_json(), payload)

    def test_invalid_upload_does_not_replace_current_session(self):
        good = self.client.post(
            "/api/session",
            data={"trace": (io.BytesIO(valid_capture()), "good.trace")},
            content_type="multipart/form-data",
        )
        self.assertEqual(good.status_code, 200)

        bad = self.client.post(
            "/api/session",
            data={"trace": (io.BytesIO(b"bad"), "bad.trace")},
            content_type="multipart/form-data",
        )
        self.assertEqual(bad.status_code, 400)
        self.assertIn("error", bad.get_json())
        self.assertEqual(self.client.get("/api/session").get_json()["filename"], "good.trace")

    def test_missing_file_and_oversize_requests_are_rejected(self):
        missing = self.client.post("/api/session", data={})
        self.assertEqual(missing.status_code, 400)

        self.app.config["MAX_CONTENT_LENGTH"] = 16
        too_large = self.client.post(
            "/api/session",
            data={"trace": (io.BytesIO(valid_capture()), "large.trace")},
            content_type="multipart/form-data",
        )
        self.assertEqual(too_large.status_code, 413)
        self.assertIn("256 MiB", too_large.get_json()["error"])

    def test_upload_limit_and_no_filesystem_route(self):
        self.assertEqual(self.app.config["MAX_CONTENT_LENGTH"], MAX_UPLOAD_BYTES)
        self.assertEqual(self.client.get("/api/file?path=../../README.md").status_code, 404)


if __name__ == "__main__":
    unittest.main()
