"""Abstract base classes (OpenSE4 script runtime): ABC and abstractmethod exist so that
code using them runs; nothing is enforced at run time.
"""


ABCMeta = type


class ABC:
    pass


def abstractmethod(funcobj):
    return funcobj


def abstractproperty(funcobj):
    return property(funcobj)


abstractclassmethod = classmethod
abstractstaticmethod = staticmethod
