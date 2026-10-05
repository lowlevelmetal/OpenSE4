"""Future statements (OpenSE4 script runtime).

Everything these name is already how the runtime behaves; "from __future__ import
annotations" in particular matches it, as annotations are never evaluated.
"""

all_feature_names = [
    "nested_scopes",
    "generators",
    "division",
    "absolute_import",
    "with_statement",
    "print_function",
    "unicode_literals",
    "generator_stop",
    "annotations",
]

nested_scopes = True
generators = True
division = True
absolute_import = True
with_statement = True
print_function = True
unicode_literals = True
generator_stop = True
annotations = True
