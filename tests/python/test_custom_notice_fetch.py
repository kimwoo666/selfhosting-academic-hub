import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

from custom_notice_fetch import extract_notices_from_html  # noqa: E402


SAMPLE_HTML = """
<html>
  <body>
    <div class="board-list">
      <a class="subject" href="/notice/101">First Notice</a>
      <a class="subject" href="/notice/102">Second Notice</a>
    </div>
    <table class="notice">
      <tr>
        <td class="title"><a href="/notice/201">Table Notice</a></td>
      </tr>
    </table>
    <div id="news">
      <div class="item">
        <a href="/notice/301"><span class="title">Nested Title</span></a>
      </div>
    </div>
  </body>
</html>
"""


class CustomNoticeFetchTests(unittest.TestCase):
    def test_class_selector_extracts_links(self) -> None:
        notices = extract_notices_from_html(SAMPLE_HTML, "https://example.edu/notices", ".board-list a.subject")
        self.assertEqual(len(notices), 2)
        self.assertEqual(notices[0]["title"], "First Notice")
        self.assertEqual(notices[0]["url"], "https://example.edu/notice/101")

    def test_descendant_selector_supports_table_layouts(self) -> None:
        notices = extract_notices_from_html(SAMPLE_HTML, "https://example.edu/notices", "table.notice td.title a")
        self.assertEqual(len(notices), 1)
        self.assertEqual(notices[0]["title"], "Table Notice")

    def test_selector_can_target_child_inside_anchor(self) -> None:
        notices = extract_notices_from_html(SAMPLE_HTML, "https://example.edu/notices", "#news .item .title")
        self.assertEqual(len(notices), 1)
        self.assertEqual(notices[0]["title"], "Nested Title")
        self.assertEqual(notices[0]["url"], "https://example.edu/notice/301")


if __name__ == "__main__":
    unittest.main()
