"""Regression checks for the HTML-only documentation site and local links."""

import tempfile
import unittest
from pathlib import Path

from validate_docs import Validator


class HtmlOnlyDocumentationTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.repo = Path(self.directory.name)
        self.docs = self.repo / "Docs"
        self.docs.mkdir()
        self.write_html("index.html", '<h1 id="overview">Documentation</h1>')

    def write_file(self, relative, content):
        path = self.docs / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def write_html(self, relative, content):
        return self.write_file(relative,
            '<!doctype html><html lang="en"><head><title>Documentation</title>'
            '<meta name="viewport" content="width=device-width, initial-scale=1">'
            '</head><body><main>' + content + '</main></body></html>')

    def write_svg(self, relative, content):
        return self.write_file(relative,
            '<svg xmlns="http://www.w3.org/2000/svg" '
            'xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 100 100" '
            'aria-labelledby="title description">'
            '<title id="title">Documentation flow</title>'
            '<desc id="description">Links to documentation</desc>' + content + '</svg>')

    def validate_site(self):
        validator = Validator(self.docs, self.repo, False)
        validator.validate_document_formats()
        validator.parse_html()
        validator.parse_xml()
        validator.validate_links({})
        return validator.findings

    def test_html_only_site_and_static_fragment_links_pass(self):
        self.write_html("chapter/index.html", '<h1 id="node-contract">Node contract</h1>')
        self.write_svg("assets/flow.svg", '<a href="../chapter/#node-contract"><text>Nodes</text></a>')
        self.write_html("index.html",
            '<h1 id="overview">Documentation</h1>'
            '<a href="#overview">Overview</a>'
            '<a href="chapter/?view=full#node%2Dcontract">Nodes</a>'
            '<img src="assets/flow.svg#title" alt="Node flow">')
        self.assertEqual(self.validate_site(), [])

    def test_nested_markdown_files_are_rejected_for_all_suffixes(self):
        for suffix in (".md", ".MD", ".markdown", ".MARKDOWN", ".mdx", ".MdX"):
            with self.subTest(suffix=suffix):
                path = self.write_file("nested/guide" + suffix, "# Guide\n")
                findings = self.validate_site()
                self.assertEqual(len(findings), 1)
                self.assertEqual(findings[0].path, "Docs/nested/guide" + suffix)
                self.assertIn("Markdown documentation is not allowed", findings[0].message)
                path.unlink()

    def test_markdown_outside_docs_is_not_rejected_by_file_scan(self):
        (self.repo / "README.md").write_text("# Source README\n", encoding="utf-8")
        self.assertEqual(self.validate_site(), [])

    def test_existing_markdown_href_is_rejected_as_file_and_link(self):
        self.write_file("guide.md", "# Guide\n")
        self.write_html("index.html", '<a href="guide.md#guide">Guide</a>')
        findings = self.validate_site()
        self.assertEqual(len(findings), 2)
        self.assertTrue(any("Markdown documentation is not allowed" in item.message for item in findings))
        self.assertTrue(any("URL targets Markdown documentation in Docs" in item.message for item in findings))

    def test_missing_markdown_href_is_rejected_for_all_suffixes(self):
        for suffix in (".md", ".MD", ".markdown", ".MARKDOWN", ".mdx", ".MdX"):
            with self.subTest(suffix=suffix):
                self.write_html("index.html", '<a href="removed' + suffix + '">Guide</a>')
                findings = self.validate_site()
                self.assertEqual(len(findings), 1)
                self.assertIn("URL targets Markdown documentation in Docs", findings[0].message)

    def test_encoded_markdown_href_with_query_and_fragment_is_rejected(self):
        self.write_html("index.html", '<a href="chapter/../old%20guide%2eM%44?view=full#overview">Guide</a>')
        findings = self.validate_site()
        self.assertEqual(len(findings), 1)
        self.assertIn("URL targets Markdown documentation in Docs", findings[0].message)

    def test_external_markdown_references_are_allowed(self):
        self.write_html("index.html",
            '<a href="https://github.com/example/project/blob/main/README.md#build">Source</a>'
            '<a href="http://example.org/reference.markdown">Reference</a>'
            '<a href="//example.org/guide.mdx">Guide</a>')
        self.assertEqual(self.validate_site(), [])

    def test_machine_local_drive_links_are_rejected(self):
        for target in (
            "D:/UnrealEngine/Engine/Source/Renderer.cpp#L20",
            "c:/Projects/Ardashir/README.md",
            "D:relative-to-current-drive.cpp",
            "D:\\Projects\\Ardashir\\README.md",
            "D%3A%2FProjects%2FArdashir%2FREADME.md",
        ):
            with self.subTest(target=target):
                self.write_html("index.html", '<a href="' + target + '">Source</a>')
                findings = self.validate_site()
                self.assertEqual(len(findings), 1)
                self.assertIn("href URL is machine-local", findings[0].message)

    def test_file_urls_are_rejected(self):
        for target in (
            "file:///D:/Projects/Ardashir/README.md",
            "FILE:///home/user/project/source.cpp",
            "file://workstation/share/source.cpp",
            "file:source.cpp",
        ):
            with self.subTest(target=target):
                self.write_html("index.html", '<a href="' + target + '">Source</a>')
                findings = self.validate_site()
                self.assertEqual(len(findings), 1)
                self.assertIn("href URL is machine-local", findings[0].message)

    def test_machine_local_image_and_svg_links_are_rejected(self):
        self.write_html("index.html", '<img src="D:/Images/flow.svg" alt="Flow">')
        self.write_svg("flow.svg", '<a xlink:href="file:///D:/Source.cpp"><text>Source</text></a>')
        findings = self.validate_site()
        self.assertEqual(len(findings), 2)
        self.assertTrue(all("URL is machine-local" in item.message for item in findings))

    def test_missing_html_target_is_rejected(self):
        self.write_html("index.html", '<a href="removed.html#overview">Guide</a>')
        findings = self.validate_site()
        self.assertEqual(len(findings), 1)
        self.assertIn("broken relative href URL", findings[0].message)

    def test_missing_static_html_fragment_is_rejected(self):
        self.write_html("guide.html", '<h1 id="overview">Guide</h1>')
        self.write_html("index.html", '<a href="guide.html?view=full#missing%2Danchor">Guide</a>')
        findings = self.validate_site()
        self.assertEqual(len(findings), 1)
        self.assertIn("broken fragment 'missing-anchor'", findings[0].message)

    def test_markdown_links_from_svg_are_rejected(self):
        self.write_svg("assets/flow.svg", '<a xlink:href="../old-guide.MARKDOWN#guide"><text>Guide</text></a>')
        findings = self.validate_site()
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].path, "Docs/assets/flow.svg")
        self.assertIn("URL targets Markdown documentation in Docs", findings[0].message)

    def test_uppercase_html_and_svg_suffixes_do_not_bypass_validation(self):
        self.write_html("GUIDE.HTML", '<a href="removed.md">Guide</a>')
        self.write_svg("FLOW.SVG", '<a href="removed.mdx"><text>Guide</text></a>')
        findings = self.validate_site()
        self.assertEqual(len(findings), 2)
        self.assertTrue(all("URL targets Markdown documentation in Docs" in item.message for item in findings))

    def test_missing_static_svg_fragment_is_rejected(self):
        self.write_svg("assets/flow.svg", "")
        self.write_html("index.html", '<img src="assets/flow.svg#missing" alt="Flow">')
        findings = self.validate_site()
        self.assertEqual(len(findings), 1)
        self.assertIn("broken fragment 'missing'", findings[0].message)

    def test_relative_links_cannot_escape_docs(self):
        (self.repo / "README.md").write_text("# Source README\n", encoding="utf-8")
        self.write_html("index.html", '<a href="../README.md#source-readme">Source</a>')
        findings = self.validate_site()
        self.assertEqual(len(findings), 1)
        self.assertIn("relative URL escapes Docs root", findings[0].message)


if __name__ == "__main__":
    unittest.main()
