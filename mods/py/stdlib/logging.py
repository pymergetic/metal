import sys

# Metal deltas from upstream micropython-lib logging.py, same spirit as the
# string.py/defaultdict.py deltas noted in docs/MICROPYTHON.md:
#   - no `import time`: MICROPY_PY_TIME is off (no wall-clock/RTC source
#     yet, see docs/TODO.md's stdlib categorization) -- LogRecord drops
#     ct/msecs, Formatter.formatTime() always returns None (no %(asctime)s).
#   - no `import io`: MICROPY_PY_IO is off -- exception() prints straight
#     through sys.print_exception(tb) instead of buffering via io.StringIO
#     first, so a traceback still reaches the console even though it can't
#     be routed through a handler's format()/emit() first.
#   - no `sys.stderr` default: MICROPY_PY_SYS_STDFILES is off -- a
#     StreamHandler with stream=None (the new default) prints via the
#     builtin print() instead of a stream object's .write().

CRITICAL = 50
ERROR = 40
WARNING = 30
INFO = 20
DEBUG = 10
NOTSET = 0

_DEFAULT_LEVEL = WARNING

_level_dict = {
    CRITICAL: "CRITICAL",
    ERROR: "ERROR",
    WARNING: "WARNING",
    INFO: "INFO",
    DEBUG: "DEBUG",
    NOTSET: "NOTSET",
}

_loggers = {}
_default_fmt = "%(levelname)s:%(name)s:%(message)s"


class LogRecord:
    def set(self, name, level, message):
        self.name = name
        self.levelno = level
        self.levelname = _level_dict[level]
        self.message = message
        self.asctime = None


class Handler:
    def __init__(self, level=NOTSET):
        self.level = level
        self.formatter = None

    def close(self):
        pass

    def setLevel(self, level):
        self.level = level

    def setFormatter(self, formatter):
        self.formatter = formatter

    def format(self, record):
        # self.formatter starts None until setFormatter() runs; every real
        # caller path (basicConfig()) sets it first -- type: ignore rather
        # than an Optional-narrowing dance neither runtime nor this
        # minimal-build MicroPython needs.
        return self.formatter.format(record)  # type: ignore[union-attr]


class StreamHandler(Handler):
    def __init__(self, stream=None):
        super().__init__()
        self.stream = stream
        self.terminator = "\n"

    def close(self):
        if self.stream is not None and hasattr(self.stream, "flush"):
            self.stream.flush()

    def emit(self, record):
        if record.levelno >= self.level:
            text = self.format(record)
            if self.stream is None:
                print(text)
            else:
                self.stream.write(text + self.terminator)


class Formatter:
    def __init__(self, fmt=None, datefmt=None):
        self.fmt = _default_fmt if fmt is None else fmt
        self.datefmt = datefmt

    def usesTime(self):
        return "asctime" in self.fmt

    def formatTime(self, datefmt, record):
        return None  # no wall-clock source on this build -- see file header

    def format(self, record):
        if self.usesTime():
            record.asctime = self.formatTime(self.datefmt, record)
        return self.fmt % {
            "name": record.name,
            "message": record.message,
            "asctime": record.asctime,
            "levelname": record.levelname,
        }


class Logger:
    def __init__(self, name, level=NOTSET):
        self.name = name
        self.level = level
        self.handlers = []
        self.record = LogRecord()

    def setLevel(self, level):
        self.level = level

    def isEnabledFor(self, level):
        return level >= self.getEffectiveLevel()

    def getEffectiveLevel(self):
        return self.level or getLogger().level or _DEFAULT_LEVEL

    def log(self, level, msg, *args):
        if self.isEnabledFor(level):
            if args:
                if isinstance(args[0], dict):
                    args = args[0]
                msg = msg % args
            self.record.set(self.name, level, msg)
            handlers = self.handlers
            if not handlers:
                handlers = getLogger().handlers
            for h in handlers:
                h.emit(self.record)

    def debug(self, msg, *args):
        self.log(DEBUG, msg, *args)

    def info(self, msg, *args):
        self.log(INFO, msg, *args)

    def warning(self, msg, *args):
        self.log(WARNING, msg, *args)

    def error(self, msg, *args):
        self.log(ERROR, msg, *args)

    def critical(self, msg, *args):
        self.log(CRITICAL, msg, *args)

    def exception(self, msg, *args, exc_info=True):
        self.log(ERROR, msg, *args)
        tb = None
        if isinstance(exc_info, BaseException):
            tb = exc_info
        elif hasattr(sys, "exc_info"):
            tb = sys.exc_info()[1]
        if tb:
            # sys.print_exception (py/modsys.c): MicroPython-only extension
            # to sys, not in CPython's typeshed stub the linter uses.
            sys.print_exception(tb)  # type: ignore[attr-defined]

    def addHandler(self, handler):
        self.handlers.append(handler)

    def hasHandlers(self):
        return len(self.handlers) > 0


def getLogger(name=None):
    if name is None:
        name = "root"
    if name not in _loggers:
        _loggers[name] = Logger(name)
        if name == "root":
            basicConfig()
    return _loggers[name]


def log(level, msg, *args):
    getLogger().log(level, msg, *args)


def debug(msg, *args):
    getLogger().debug(msg, *args)


def info(msg, *args):
    getLogger().info(msg, *args)


def warning(msg, *args):
    getLogger().warning(msg, *args)


def error(msg, *args):
    getLogger().error(msg, *args)


def critical(msg, *args):
    getLogger().critical(msg, *args)


def exception(msg, *args):
    getLogger().exception(msg, *args)


def shutdown():
    for k, logger in _loggers.items():
        for h in logger.handlers:
            h.close()
        _loggers.pop(logger, None)


def addLevelName(level, name):
    _level_dict[level] = name


def basicConfig(format=None, level=WARNING, stream=None, force=False):
    if "root" not in _loggers:
        _loggers["root"] = Logger("root")

    logger = _loggers["root"]

    if force or not logger.handlers:
        for h in logger.handlers:
            h.close()
        logger.handlers = []

        handler = StreamHandler(stream)
        handler.setLevel(level)
        handler.setFormatter(Formatter(format))

        logger.setLevel(level)
        logger.addHandler(handler)


if hasattr(sys, "atexit"):
    sys.atexit(shutdown)  # type: ignore[attr-defined]
