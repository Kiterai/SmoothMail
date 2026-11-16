import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class DocumentStructureTests(unittest.TestCase):
    def test_architecture_section_order_and_two_command_regeneration(self):
        content = (ROOT / "docs/dev/architecture.md").read_text(encoding="utf-8")
        self.assertEqual(re.findall(r"^## (.+)$", content, re.M), [
            "モジュール", "外部ライブラリ・ツール", "自動生成した依存図",
            "ターゲット別ソース一覧（自動生成）", "依存図および表の再生成方法", "依存図および表の生成手法"])
        instructions = content.split("## 依存図および表の再生成方法\n", 1)[1].split("\n## ", 1)[0]
        self.assertEqual(instructions.strip(), "```text\npython tools/setup_architecture.py\n"
                         "python tools/generate_architecture.py\n```")
        before_methods = content.split("## 依存図および表の生成手法", 1)[0]
        self.assertNotIn("unflatten -l", before_methods)
        self.assertNotIn("構築済み", instructions)

    def test_generated_table_rows_are_single_source_records(self):
        content = (ROOT / "docs/dev/architecture.md").read_text(encoding="utf-8")
        tables = content.split("<!-- BEGIN GENERATED SOURCE TABLES -->", 1)[1].split(
            "<!-- END GENERATED SOURCE TABLES -->", 1)[0]
        records = [line for line in tables.splitlines() if line.startswith("| ") and "ソースファイル" not in line]
        self.assertTrue(records)
        for row in records:
            cells = [cell.strip() for cell in row.strip("|").split("|")]
            self.assertEqual(len(cells), 4)
            self.assertTrue(cells[0].startswith("src/"), row)
            self.assertNotIn("::", cells[1] + cells[2])
            self.assertNotIn("(", cells[2])
        self.assertIn("<br>", tables)

    def test_usage_build_run_and_distribution_order(self):
        usage = (ROOT / "docs/usage.md").read_text(encoding="utf-8")
        positions = [usage.index("\n### " + heading) for heading in (
            "メールを読む", "情報を更新する", "アカウントを登録する")]
        self.assertEqual(positions, sorted(positions))
        expected = {
            "build.md": ["準備・要件", "デバッグビルド", "リリースビルド"],
            "run.md": ["デバッグ実行", "リリースビルド実行"],
            "distribution.md": ["最終配布物の生成", "ライセンス注意事項", "配布手順"],
        }
        for name, headings in expected.items():
            with self.subTest(document=name):
                content = (ROOT / "docs/dev" / name).read_text(encoding="utf-8")
                self.assertEqual(re.findall(r"^## (.+)$", content, re.M), headings)


if __name__ == "__main__":
    unittest.main()
