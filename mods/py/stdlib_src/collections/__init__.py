# Replace built-in collections module.
from ucollections import *

# Provide optional dependencies (which may be installed separately).
from .defaultdict import defaultdict


class MutableMapping:
    pass
