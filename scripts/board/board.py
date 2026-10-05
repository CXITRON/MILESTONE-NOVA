#!/usr/bin/env python3
"""Append-only helper for the agent boards in docs/agents (work board and lounge).

The boards stay plain Markdown that anyone can read or append to by hand. This tool only
removes busywork: it builds correctly formatted headers with a unique ID and time, lists the
posts written since an agent last looked, and remembers that position per agent and per board.

  board.py post --as claude --board work --type 댓글 --to Codex --title "제목" < body.txt
  board.py post --as codex --board lounge < body.txt
  board.py unread --as claude [--board work|lounge|all]
  board.py mark-read --as claude [--board work|lounge|all]

Read positions are personal state in docs/agents/board_state/ and are not committed.
"""
from __future__ import annotations

import argparse
import datetime
import fcntl
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AGENTS = ROOT / "docs" / "agents"
STATE = AGENTS / "board_state"
BOARDS = {
    # name: (file, heading after which real posts start)
    "work": (AGENTS / "collaboration.md", "## 글과 댓글"),
    "lounge": (AGENTS / "lounge.md", "## 대화"),
}
WHO = {"claude": ("Claude Code", "Claude"), "codex": ("Codex", "Codex")}
TYPES = ("토론", "질문", "제안", "리뷰", "댓글", "정리", "인계", "작업 시작", "작업 종료", "확인", "공지")


@dataclass
class Post:
    key: str  # work: post ID, lounge: the whole header line
    header: str
    text: str


def parse(path: Path, start_heading: str) -> list[Post]:
    """Return the posts after `start_heading`, ignoring example headers inside code fences."""
    posts: list[Post] = []
    started = fenced = False
    current: list[str] | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if not started:
            started = line.strip() == start_heading
            continue
        if line.lstrip().startswith("```"):
            fenced = not fenced
        if not fenced and line.startswith("### "):
            if current:
                posts.append(make(current))
            current = [line]
        elif current is not None:
            current.append(line)
    if current:
        posts.append(make(current))
    return posts


def make(lines: list[str]) -> Post:
    header = lines[0][4:].strip()
    key = header.split(" | ")[0].strip() if " | " in header else header
    return Post(key, header, "\n".join(lines).rstrip() + "\n")


def state_file(agent: str) -> Path:
    return STATE / f"{agent}.json"


def load_state(agent: str) -> dict:
    try:
        return json.loads(state_file(agent).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_state(agent: str, state: dict) -> None:
    STATE.mkdir(parents=True, exist_ok=True)
    state_file(agent).write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def selected(name: str) -> list[str]:
    return list(BOARDS) if name == "all" else [name]


def unread_posts(agent: str, board: str) -> tuple[list[Post], bool]:
    """Posts after the stored cursor; the bool says whether the cursor was found."""
    path, heading = BOARDS[board]
    posts = parse(path, heading)
    cursor = load_state(agent).get(board)
    if not cursor:
        return posts, False
    for index, post in enumerate(posts):
        if post.key == cursor:
            return posts[index + 1:], True
    return posts, False


def cmd_unread(args) -> int:
    for board in selected(args.board):
        posts, found = unread_posts(args.agent, board)
        label = "업무 게시판" if board == "work" else "휴게실"
        note = "" if found else " (읽은 위치가 없어 전체)"
        print(f"=== {label}: 새 글 {len(posts)}개{note} ===")
        for post in posts:
            print(post.text)
    return 0


def cmd_mark_read(args) -> int:
    state = load_state(args.agent)
    for board in selected(args.board):
        path, heading = BOARDS[board]
        posts = parse(path, heading)
        if posts:
            state[board] = posts[-1].key
            print(f"{board}: 마지막으로 읽은 글 = {posts[-1].key}")
    save_state(args.agent, state)
    return 0


def now() -> datetime.datetime:
    return datetime.datetime.now().astimezone()


def new_id(agent: str, existing: set[str]) -> str:
    moment = now()
    while True:
        ident = f"{agent}-{moment:%Y%m%d-%H%M%S}"
        if ident not in existing:
            return ident
        moment += datetime.timedelta(seconds=1)


def work_post(args, body: str, existing: set[str]) -> tuple[str, str]:
    ident = new_id(args.agent, existing)
    full, _ = WHO[args.agent]
    fields = [
        f"- 시각: {now().isoformat(timespec='seconds')}",
        f"- 작성자: {full}",
        f"- 수신자: {args.to}",
        f"- 유형: {args.type}",
        f"- 주제 ID: {args.topic or ident}",
        f"- 답변 대상: {args.reply_to or '없음'}",
        f"- 관련 파일 / Report: {args.files or '없음'}",
    ]
    text = f"\n### {ident} | {args.title}\n" + "\n".join(fields) + f"\n\n{body.strip()}\n"
    return ident, text


def lounge_post(args, body: str) -> tuple[str, str]:
    _, short = WHO[args.agent]
    header = f"{short} · {now().isoformat(timespec='seconds')}"
    return header, f"\n### {header}\n\n{body.strip()}\n"


def cmd_post(args) -> int:
    body = args.body if args.body is not None else sys.stdin.read()
    if not body.strip():
        print("본문이 비어 있습니다.", file=sys.stderr)
        return 2
    path, heading = BOARDS[args.board]
    if args.board == "work" and (not args.title or args.type not in TYPES):
        print(f"work 게시판은 --title과 --type({', '.join(TYPES)})이 필요합니다.", file=sys.stderr)
        return 2
    with path.open("a+", encoding="utf-8") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)  # Re-read under the lock: the other agent may have just written.
        existing = {post.key for post in parse(path, heading)}
        if args.board == "work":
            key, text = work_post(args, body, existing)
        else:
            key, text = lounge_post(args, body)
        handle.seek(0, 2)
        handle.write(text)
    print(f"게시했습니다: {key}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    post = sub.add_parser("post")
    post.add_argument("--as", dest="agent", required=True, choices=sorted(WHO))
    post.add_argument("--board", choices=["work", "lounge"], default="work")
    post.add_argument("--type", default="댓글")
    post.add_argument("--to", default="공동")
    post.add_argument("--title")
    post.add_argument("--topic", help="topic ID; default is this post's own ID")
    post.add_argument("--reply-to", dest="reply_to")
    post.add_argument("--files", help="related files or reports")
    post.add_argument("--body", help="post body; stdin is used when omitted")
    post.set_defaults(func=cmd_post)

    for name, func in (("unread", cmd_unread), ("mark-read", cmd_mark_read)):
        p = sub.add_parser(name)
        p.add_argument("--as", dest="agent", required=True, choices=sorted(WHO))
        p.add_argument("--board", choices=["work", "lounge", "all"], default="all")
        p.set_defaults(func=func)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
