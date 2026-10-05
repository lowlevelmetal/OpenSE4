"""Regular expressions (OpenSE4 script runtime): MicroPython's re module, plus the
functions of CPython's that it lacks: findall, finditer, fullmatch, escape, split
with a maximum, and compiled patterns with the same methods.

The engine is a small backtracking matcher: . [] [^] * + ? *? +? | ( ) ^ $ \\d \\w \\s
and their capitals work; counted repetition {m,n}, named or non-capturing groups,
lookarounds and flags do not. fullmatch accepts a match that ends at the end of the
text, so with alternatives it may miss a longer full match that CPython finds.
"""

import ure as _ure

__all__ = ["compile", "match", "search", "fullmatch", "sub", "split", "findall", "finditer", "escape", "purge"]

DEBUG = 0


class Pattern:
    def __init__(self, pattern, flags=0):
        if flags:
            raise ValueError("regular expression flags are not supported")
        self.pattern = pattern
        self.flags = 0
        self._re = _ure.compile(pattern)

    def match(self, string, pos=0, endpos=None):
        if endpos is None:
            return self._re.match(string, pos)
        return self._re.match(string, pos, endpos)

    def search(self, string, pos=0, endpos=None):
        if endpos is None:
            return self._re.search(string, pos)
        return self._re.search(string, pos, endpos)

    def fullmatch(self, string, pos=0, endpos=None):
        end = len(string) if endpos is None else min(endpos, len(string))
        m = self.match(string, pos, end)
        if m is not None and m.end() == end:
            return m
        return None

    def sub(self, repl, string, count=0):
        return self._re.sub(repl, string, count)

    def subn(self, repl, string, count=0):
        n = 0
        for _ in self.finditer(string):
            n += 1
            if count and n == count:
                break
        return self._re.sub(repl, string, count), n

    def split(self, string, maxsplit=0):
        return self._re.split(string, maxsplit)

    def finditer(self, string, pos=0, endpos=None):
        end = len(string) if endpos is None else min(endpos, len(string))
        while pos <= end:
            m = self._re.search(string, pos, end)
            if m is None:
                return
            yield m
            pos = m.end() if m.end() > m.start() else m.end() + 1

    def findall(self, string, pos=0, endpos=None):
        result = []
        for m in self.finditer(string, pos, endpos):
            groups = m.groups()
            if not groups:
                result.append(m.group(0))
            elif len(groups) == 1:
                result.append("" if groups[0] is None else groups[0])
            else:
                result.append(tuple("" if g is None else g for g in groups))
        return result

    def __repr__(self):
        return "re.compile({!r})".format(self.pattern)


def compile(pattern, flags=0):
    if isinstance(pattern, Pattern):
        return pattern
    return Pattern(pattern, flags)


def match(pattern, string, flags=0):
    return compile(pattern, flags).match(string)


def search(pattern, string, flags=0):
    return compile(pattern, flags).search(string)


def fullmatch(pattern, string, flags=0):
    return compile(pattern, flags).fullmatch(string)


def sub(pattern, repl, string, count=0, flags=0):
    return compile(pattern, flags).sub(repl, string, count)


def subn(pattern, repl, string, count=0, flags=0):
    return compile(pattern, flags).subn(repl, string, count)


def split(pattern, string, maxsplit=0, flags=0):
    return compile(pattern, flags).split(string, maxsplit)


def finditer(pattern, string, flags=0):
    return compile(pattern, flags).finditer(string)


def findall(pattern, string, flags=0):
    return compile(pattern, flags).findall(string)


_special = set("\\.^$*+?{}[]|()")


def escape(pattern):
    return "".join("\\" + c if c in _special else c for c in pattern)


def purge():
    pass
