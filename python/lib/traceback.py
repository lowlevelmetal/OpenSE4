"""Tracebacks as text (OpenSE4 script runtime): the commonly used part of CPython's
traceback module, so that a script can log an exception it caught.

The runtime writes a traceback as MicroPython does: the calls, most recent last, with
the file and line of each, and the exception's type and message. There are no source
lines, and chained exceptions (raise ... from ...) are not shown.
"""

import io
import sys

_UNSET = object()


def _exception(exc, value):
    # format_exception(exc) and format_exception(type, value, tb) are both accepted.
    if value is not _UNSET and value is not None:
        return value
    if isinstance(exc, BaseException):
        return exc
    if exc is None:
        return None
    raise TypeError("format_exception() takes an exception or (type, value, traceback)")


def _text(value):
    if value is None:
        return "NoneType: None\n"
    buf = io.StringIO()
    sys.print_exception(value, buf)
    return buf.getvalue()


def _lines(text):
    lines = []
    start = 0
    while start < len(text):
        end = text.find("\n", start)
        if end < 0:
            lines.append(text[start:] + "\n")
            break
        lines.append(text[start:end + 1])
        start = end + 1
    return lines


def format_exception(exc, value=_UNSET, tb=_UNSET, limit=None, chain=True):
    """The traceback of an exception, as a list of lines that each end in a newline."""
    return _lines(_text(_exception(exc, value)))


def format_exception_only(exc, value=_UNSET):
    """The last line of the traceback: the exception's type and message."""
    value = _exception(exc, value)
    if value is None:
        return ["NoneType: None\n"]
    message = str(value)
    name = type(value).__name__
    return [name + (": " + message if message else "") + "\n"]


def format_exc(limit=None, chain=True):
    """The traceback of the exception being handled, as one text."""
    return _text(sys.exc_info()[1])


def print_exception(exc, value=_UNSET, tb=_UNSET, limit=None, file=None, chain=True):
    """Prints the traceback of an exception (to the script's output, or `file`)."""
    text = _text(_exception(exc, value))
    if file is None:
        print(text, end="")
    else:
        file.write(text)


def print_exc(limit=None, file=None, chain=True):
    """Prints the traceback of the exception being handled."""
    text = _text(sys.exc_info()[1])
    if file is None:
        print(text, end="")
    else:
        file.write(text)
