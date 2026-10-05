"""The parts of MicroPython's micropython module that code meant for it commonly
uses (OpenSE4 script runtime): const() and the code-emitter decorators, which
change nothing here.
"""


def const(value):
    return value


def native(f):
    return f


def viper(f):
    return f


def bytecode(f):
    return f
