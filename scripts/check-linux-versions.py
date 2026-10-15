#!/usr/bin/env python3
"""Read current release versions from upstream project release pages."""

import json
import re
import sys
import urllib.request
from html.parser import HTMLParser


def fetch(url, accept="text/html"):
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "SimRV-linux-version-check/1.0", "Accept": accept},
    )
    with urllib.request.urlopen(request, timeout=15) as response:
        return response.read().decode("utf-8", "replace")


def version_key(value):
    return tuple(int(part) for part in re.findall(r"\d+", value))


class TableRows(HTMLParser):
    def __init__(self):
        super().__init__()
        self.rows = []
        self.row = []
        self.cell = None

    def handle_starttag(self, tag, attrs):
        if tag == "tr":
            self.row = []
        elif tag in ("td", "th"):
            self.cell = []

    def handle_data(self, data):
        if self.cell is not None:
            self.cell.append(data.strip())

    def handle_endtag(self, tag):
        if tag in ("td", "th") and self.cell is not None:
            self.row.append(" ".join(part for part in self.cell if part))
            self.cell = None
        elif tag == "tr" and self.row:
            self.rows.append(self.row)


def main():
    found = {}
    errors = {}

    def record(key, action):
        try:
            found[key] = action()
        except Exception as exc:  # network, HTTP, or upstream format change
            errors[key] = str(exc)

    def kernel():
        data = json.loads(fetch("https://www.kernel.org/releases.json", "application/json"))
        return data["latest_stable"]["version"]

    def opensbi():
        data = json.loads(
            fetch(
                "https://api.github.com/repos/riscv-software-src/opensbi/releases/latest",
                "application/vnd.github+json",
            )
        )
        tag = data["tag_name"]
        return tag[1:] if tag.startswith("v") else tag

    def busybox():
        page = fetch("https://busybox.net/downloads/")
        releases = re.findall(r"busybox-(\d+(?:\.\d+)+)\.tar\.bz2", page)
        if not releases:
            raise ValueError("no release archives found")
        return max(releases, key=version_key)

    def alpine():
        page = fetch(
            "https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/riscv64/"
        )
        releases = re.findall(r"alpine-minirootfs-(\d+(?:\.\d+)+)-riscv64\.tar\.gz", page)
        if not releases:
            raise ValueError("no riscv64 minirootfs archives found")
        return max(releases, key=version_key)

    def buildroot_releases():
        parser = TableRows()
        parser.feed(fetch("https://buildroot.org/download.html"))
        releases = {}
        for row in parser.rows:
            if row and row[0].lower().startswith("stable"):
                match = re.search(r"\b(\d{4}\.\d{2}(?:\.\d+)?)\b", " ".join(row))
                if match:
                    releases["buildroot_stable"] = match.group(1)
            elif row and row[0].lower().startswith("long-term support"):
                match = re.search(r"\b(\d{4}\.\d{2}(?:\.\d+)?)\b", " ".join(row))
                if match:
                    releases["buildroot_lts"] = match.group(1)
        if not releases:
            raise ValueError("stable/LTS release rows not found")
        return releases

    record("linux", kernel)
    record("opensbi", opensbi)
    record("busybox", busybox)
    record("alpine", alpine)
    try:
        found.update(buildroot_releases())
    except Exception as exc:
        errors["buildroot"] = str(exc)

    print(json.dumps({"versions": found, "errors": errors}, sort_keys=True))
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main())
