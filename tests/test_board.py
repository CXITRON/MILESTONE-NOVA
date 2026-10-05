"""Board helper: header format, unique IDs, per-agent cursors, code-fence handling."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('board', ROOT / 'scripts/board/board.py')
board = importlib.util.module_from_spec(spec)
sys.modules['board'] = board  # dataclasses looks the defining module up here
spec.loader.exec_module(board)

WORK = """# 게시판

예시 형식:

```markdown
### <agent-YYYYMMDD-HHMMSS> | <짧은 제목>
- 시각: 예시
```

## 글과 댓글

### codex-20260101-000000 | 첫 글
- 작성자: Codex

첫 본문.
"""

LOUNGE = """# 휴게실

## 대화

### Codex · 2026-01-01T00:00:00+09:00

안녕.
"""


class BoardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        (root / 'collaboration.md').write_text(WORK, encoding='utf-8')
        (root / 'lounge.md').write_text(LOUNGE, encoding='utf-8')
        self.saved = (board.BOARDS, board.STATE)
        board.BOARDS = {'work': (root / 'collaboration.md', '## 글과 댓글'),
                        'lounge': (root / 'lounge.md', '## 대화')}
        board.STATE = root / 'state'

    def tearDown(self):
        board.BOARDS, board.STATE = self.saved
        self.tmp.cleanup()

    def posts(self, name):
        path, heading = board.BOARDS[name]
        return board.parse(path, heading)

    def run_cli(self, *argv, body=None):
        args = list(argv) + (['--body', body] if body is not None else [])
        return board.main(args)

    def test_example_header_inside_fence_is_not_a_post(self):
        self.assertEqual([p.key for p in self.posts('work')], ['codex-20260101-000000'])
        self.assertEqual(len(self.posts('lounge')), 1)

    def test_post_appends_with_unique_ids_and_keeps_old_text(self):
        before = board.BOARDS['work'][0].read_text(encoding='utf-8')
        for _ in range(2):
            self.assertEqual(0, self.run_cli('post', '--as', 'claude', '--type', '댓글', '--to', 'Codex',
                                             '--title', '답', '--reply-to', 'codex-20260101-000000',
                                             body='확인했습니다.'))
        after = board.BOARDS['work'][0].read_text(encoding='utf-8')
        self.assertTrue(after.startswith(before))
        keys = [p.key for p in self.posts('work')]
        self.assertEqual(3, len(keys))
        self.assertEqual(3, len(set(keys)))
        self.assertIn('- 작성자: Claude Code', after)
        self.assertIn('- 답변 대상: codex-20260101-000000', after)

    def test_work_post_needs_title_and_known_type(self):
        self.assertEqual(2, self.run_cli('post', '--as', 'claude', '--type', '댓글', body='x'))
        self.assertEqual(2, self.run_cli('post', '--as', 'claude', '--type', '없는유형', '--title', 't', body='x'))
        self.assertEqual(2, self.run_cli('post', '--as', 'claude', '--board', 'lounge', body='  '))

    def test_lounge_post_uses_name_and_time_header(self):
        self.assertEqual(0, self.run_cli('post', '--as', 'claude', '--board', 'lounge', body='반가워.'))
        header = self.posts('lounge')[-1].header
        self.assertTrue(header.startswith('Claude · 20'))

    def test_cursors_are_per_agent_and_per_board(self):
        posts, found = board.unread_posts('claude', 'work')
        self.assertFalse(found)
        self.assertEqual(1, len(posts))
        self.assertEqual(0, board.main(['mark-read', '--as', 'claude', '--board', 'work']))
        self.assertEqual(([], True), board.unread_posts('claude', 'work'))
        self.assertEqual(1, len(board.unread_posts('claude', 'lounge')[0]))  # other board untouched
        self.assertEqual(1, len(board.unread_posts('codex', 'work')[0]))  # other agent untouched
        self.run_cli('post', '--as', 'codex', '--type', '인계', '--to', 'Claude Code', '--title', '새 글', body='b')
        self.assertEqual(['codex'], [p.key.split('-')[0] for p in board.unread_posts('claude', 'work')[0]])


if __name__ == '__main__':
    unittest.main()
