import argparse
import json
import sys
from dataclasses import dataclass, field
from html.parser import HTMLParser
from typing import Dict, List, Optional, Tuple
from urllib.parse import urljoin
from urllib.request import Request, urlopen


VOID_TAGS = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr",
}


@dataclass
class Node:
    tag: str
    attrs: Dict[str, str]
    parent: Optional["Node"] = None
    children: List["Node"] = field(default_factory=list)
    text_parts: List[str] = field(default_factory=list)

    def append_child(self, child: "Node") -> None:
        self.children.append(child)

    def add_text(self, text: str) -> None:
        if text:
            self.text_parts.append(text)

    def text_content(self) -> str:
        parts = list(self.text_parts)
        for child in self.children:
            parts.append(child.text_content())
        return "".join(parts)


class DomParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.root = Node(tag="document", attrs={})
        self.stack: List[Node] = [self.root]

    def handle_starttag(self, tag: str, attrs: List[Tuple[str, Optional[str]]]) -> None:
        node = Node(tag=tag.lower(), attrs={key.lower(): value or "" for key, value in attrs}, parent=self.stack[-1])
        self.stack[-1].append_child(node)
        if node.tag not in VOID_TAGS:
            self.stack.append(node)

    def handle_endtag(self, tag: str) -> None:
        lowered = tag.lower()
        while len(self.stack) > 1:
            node = self.stack.pop()
            if node.tag == lowered:
                break

    def handle_data(self, data: str) -> None:
        self.stack[-1].add_text(data)


@dataclass
class SimpleSelector:
    tag: Optional[str]
    id_value: Optional[str]
    classes: List[str]
    attrs: List[Tuple[str, Optional[str]]]


def normalize_whitespace(value: str) -> str:
    return " ".join(value.split())


def split_selector(selector: str) -> List[str]:
    tokens: List[str] = []
    current: List[str] = []
    bracket_depth = 0

    normalized = selector.replace(">", " ")
    for char in normalized:
        if char == "[":
            bracket_depth += 1
        elif char == "]" and bracket_depth > 0:
            bracket_depth -= 1

        if char.isspace() and bracket_depth == 0:
            if current:
                tokens.append("".join(current))
                current = []
            continue
        current.append(char)

    if current:
        tokens.append("".join(current))
    return tokens


def parse_simple_selector(token: str) -> SimpleSelector:
    tag: Optional[str] = None
    id_value: Optional[str] = None
    classes: List[str] = []
    attrs: List[Tuple[str, Optional[str]]] = []

    i = 0
    if token and token[0].isalpha():
        start = 0
        while i < len(token) and (token[i].isalnum() or token[i] in {"-", "_"}):
            i += 1
        tag = token[start:i].lower()

    while i < len(token):
        if token[i] == ".":
            i += 1
            start = i
            while i < len(token) and (token[i].isalnum() or token[i] in {"-", "_"}):
                i += 1
            classes.append(token[start:i])
            continue

        if token[i] == "#":
            i += 1
            start = i
            while i < len(token) and (token[i].isalnum() or token[i] in {"-", "_"}):
                i += 1
            id_value = token[start:i]
            continue

        if token[i] == "[":
            end = token.find("]", i)
            if end == -1:
                break
            content = token[i + 1:end]
            if "=" in content:
                name, raw_value = content.split("=", 1)
                value = raw_value.strip().strip("\"'")
                attrs.append((name.strip().lower(), value))
            else:
                attrs.append((content.strip().lower(), None))
            i = end + 1
            continue

        i += 1

    return SimpleSelector(tag=tag, id_value=id_value, classes=classes, attrs=attrs)


def matches_simple(node: Node, selector: SimpleSelector) -> bool:
    if selector.tag and node.tag != selector.tag:
        return False

    if selector.id_value and node.attrs.get("id", "") != selector.id_value:
        return False

    node_classes = set(node.attrs.get("class", "").split())
    if any(class_name not in node_classes for class_name in selector.classes):
        return False

    for attr_name, attr_value in selector.attrs:
        if attr_name not in node.attrs:
            return False
        if attr_value is not None and node.attrs.get(attr_name) != attr_value:
            return False

    return True


def matches_selector_chain(node: Node, selector_chain: List[SimpleSelector]) -> bool:
    current: Optional[Node] = node
    index = len(selector_chain) - 1

    while current is not None and index >= 0:
        if matches_simple(current, selector_chain[index]):
            index -= 1
        current = current.parent

    return index < 0


def walk(node: Node) -> List[Node]:
    nodes: List[Node] = []
    for child in node.children:
        nodes.append(child)
        nodes.extend(walk(child))
    return nodes


def select_all(root: Node, selector: str) -> List[Node]:
    selector_tokens = split_selector(selector)
    if not selector_tokens:
        return []

    selector_chain = [parse_simple_selector(token) for token in selector_tokens]
    return [node for node in walk(root) if matches_selector_chain(node, selector_chain)]


def find_href(node: Node) -> str:
    if "href" in node.attrs:
        return node.attrs["href"]

    for child in walk(node):
        if child.tag == "a" and "href" in child.attrs:
            return child.attrs["href"]

    current = node.parent
    while current is not None:
        if current.tag == "a" and "href" in current.attrs:
            return current.attrs["href"]
        current = current.parent

    return ""


def extract_notices_from_html(html: str, base_url: str, selector: str, limit: int = 20) -> List[Dict[str, str]]:
    parser = DomParser()
    parser.feed(html)
    matches = select_all(parser.root, selector)

    notices: List[Dict[str, str]] = []
    seen = set()
    for node in matches:
        title = normalize_whitespace(node.text_content())
        href = find_href(node)
        absolute_url = urljoin(base_url, href) if href else base_url

        if not title:
            continue

        dedupe_key = (title, absolute_url)
        if dedupe_key in seen:
            continue
        seen.add(dedupe_key)

        notices.append({
            "title": title,
            "url": absolute_url,
            "date": "",
        })
        if len(notices) >= limit:
            break

    return notices


def fetch_html(url: str) -> str:
    request = Request(
        url,
        headers={
            "User-Agent": (
                "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36"
            )
        },
    )
    with urlopen(request, timeout=20) as response:
        charset = response.headers.get_content_charset() or "utf-8"
        return response.read().decode(charset, errors="replace")


def load_config(args: argparse.Namespace) -> Tuple[str, str, int]:
    if args.config:
        with open(args.config, "r", encoding="utf-8") as handle:
            payload = json.load(handle)
        return payload["url"], payload["selector"], int(payload.get("limit", 20))

    if not args.url or not args.selector:
        raise ValueError("Both --url and --selector are required when --config is not used.")
    return args.url, args.selector, int(args.limit)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", help="Path to JSON config containing url and selector")
    parser.add_argument("--url")
    parser.add_argument("--selector")
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args()

    try:
        url, selector, limit = load_config(args)
        html = fetch_html(url)
        notices = extract_notices_from_html(html, url, selector, limit=max(1, limit))
        print(json.dumps({"success": True, "notices": notices}, ensure_ascii=False))
        return 0
    except Exception as exc:
        print(json.dumps({"success": False, "message": str(exc), "notices": []}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
