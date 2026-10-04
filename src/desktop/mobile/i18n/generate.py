#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Drawpile Mobile (fork): generates mobileui_<lang>.ts.

Two kinds of entries end up in the generated catalogs:

1. The fork's own strings (contexts "mobile::*"). Their sources come from
   mobileui_template.ts, which is produced by lupdate:

     cd src && lupdate -no-obsolete -locations none desktop/mobile -I . \\
         -ts desktop/mobile/i18n/mobileui_template.ts

   Translations live in data/mobile_strings.json.

2. Fixes for upstream strings that are untranslated in Drawpile's own Russian
   and Ukrainian catalogs (data/upstream_fixes.json). An entry is only emitted
   while upstream still lacks a translation for it, so translations done
   upstream automatically take over after merging. Entries listed with
   "override": true are emitted regardless, to replace wrong translations.

The generated catalogs get bundled after upstream's, so they take precedence.
"""
import json
import os
import sys
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
LANGS = ["ru_RU", "uk_UA"]
UPSTREAM = [
    os.path.join(SRC, "desktop", "i18n", "drawpile_{}.ts"),
    os.path.join(SRC, "libclient", "i18n", "libclient_{}.ts"),
]


def untranslated_keys(lang):
    keys = set()
    for pattern in UPSTREAM:
        tree = ET.parse(pattern.format(lang))
        for context in tree.getroot().findall("context"):
            name = context.find("name").text
            for message in context.findall("message"):
                source = message.find("source").text
                comment = message.find("comment")
                comment = comment.text if comment is not None else None
                translation = message.find("translation")
                missing = translation is None or translation.get("type") in (
                    "unfinished",
                    "vanished",
                    "obsolete",
                )
                if not missing:
                    texts = [translation.text] + [
                        n.text for n in translation.findall("numerusform")
                    ]
                    missing = not any(t for t in texts)
                if missing:
                    keys.add((name, source, comment))
    return keys


def template_messages():
    tree = ET.parse(os.path.join(HERE, "mobileui_template.ts"))
    out = []
    for context in tree.getroot().findall("context"):
        name = context.find("name").text
        for message in context.findall("message"):
            comment = message.find("comment")
            out.append(
                {
                    "context": name,
                    "source": message.find("source").text,
                    "comment": comment.text if comment is not None else None,
                    "numerus": message.get("numerus") == "yes",
                }
            )
    return out


def message_xml(entry, translation):
    numerus = ' numerus="yes"' if entry["numerus"] else ""
    lines = ["    <message{}>".format(numerus)]
    lines.append("        <source>{}</source>".format(escape(entry["source"])))
    if entry.get("comment"):
        lines.append("        <comment>{}</comment>".format(escape(entry["comment"])))
    if translation is None:
        lines.append('        <translation type="unfinished"></translation>')
    elif entry["numerus"]:
        forms = translation if isinstance(translation, list) else [translation] * 3
        lines.append("        <translation>")
        for form in forms:
            lines.append("            <numerusform>{}</numerusform>".format(escape(form)))
        lines.append("        </translation>")
    else:
        lines.append("        <translation>{}</translation>".format(escape(translation)))
    lines.append("    </message>")
    return "\n".join(lines)


def main():
    with open(os.path.join(HERE, "data", "mobile_strings.json"), encoding="utf-8") as f:
        mobile = {(e["context"], e["source"]): e for e in json.load(f)}
    with open(os.path.join(HERE, "data", "upstream_fixes.json"), encoding="utf-8") as f:
        fixes = json.load(f)
    template = template_messages()

    ok = True
    for lang in LANGS:
        missing = untranslated_keys(lang)
        contexts = {}
        for entry in template:
            t = mobile.get((entry["context"], entry["source"]))
            translation = t[lang] if t else None
            if translation is None:
                print("{}: missing translation for {!r} in {}".format(
                    lang, entry["source"], entry["context"]), file=sys.stderr)
                ok = False
            contexts.setdefault(entry["context"], []).append(message_xml(entry, translation))
        emitted_fixes = 0
        seen = set()
        for entry in fixes:
            key = (entry["context"], entry["source"], entry["comment"])
            if key in seen:
                continue
            seen.add(key)
            override = entry.get("override")
            if (override is True or (isinstance(override, list) and lang in override)) or key in missing:
                contexts.setdefault(entry["context"], []).append(
                    message_xml(entry, entry[lang]))
                emitted_fixes += 1
        parts = ['<?xml version="1.0" encoding="utf-8"?>', "<!DOCTYPE TS>",
                 '<TS version="2.1" language="{}">'.format(lang)]
        for name in sorted(contexts):
            parts.append("<context>")
            parts.append("    <name>{}</name>".format(escape(name)))
            parts.extend(contexts[name])
            parts.append("</context>")
        parts.append("</TS>")
        path = os.path.join(HERE, "mobileui_{}.ts".format(lang))
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(parts) + "\n")
        print("{}: {} fork strings, {} upstream fixes".format(
            lang, len(template), emitted_fixes))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
