"""Text wrapping and filling.
"""

# Copyright (C) 1999-2001 Gregory P. Ward.
# Copyright (C) 2002, 2003 Python Software Foundation.
# Written by Greg Ward <gward@python.net>

import re

__all__ = ["TextWrapper", "wrap", "fill", "dedent", "indent", "shorten"]

# Hardcode the recognized whitespace characters to the US-ASCII
# whitespace characters.  The main reason for doing this is that in
# ISO-8859-1, 0xa0 is non-breaking whitespace, so in certain locales
# that character winds up in string.whitespace.  Respecting
# string.whitespace in those cases would 1) make textwrap treat 0xa0 the
# same as any other whitespace char, which is clearly wrong (it's a
# *non-breaking* space), 2) possibly cause problems with Unicode,
# since 0xa0 is not in range(128).
_whitespace = "\t\n\x0b\x0c\r "


def _is_word_char(c):
    return c.isalnum() or c == "_"


def _expandtabs_metal(text, tabsize):
    # str.expandtabs()/str.translate() don't exist in this build's
    # MicroPython (upstream gap, not a config flag -- py/objstr.c has no
    # such methods at all). Column-tracking tab expansion, scanning for
    # embedded newlines/carriage-returns to reset the column same as
    # CPython's expandtabs().
    if tabsize <= 0:
        return text.replace("\t", "")
    out = []
    col = 0
    for c in text:
        if c == "\t":
            n = tabsize - (col % tabsize)
            out.append(" " * n)
            col += n
        elif c == "\n" or c == "\r":
            out.append(c)
            col = 0
        else:
            out.append(c)
            col += 1
    return "".join(out)


def _translate_ws_metal(text, ws_chars, replacement):
    # Stand-in for str.translate(dict) -- map every char in ws_chars to
    # replacement, everything else passes through unchanged.
    out = []
    for c in text:
        out.append(replacement if c in ws_chars else c)
    return "".join(out)


def _split_hyphens_metal(word):
    # word has no whitespace in it (caller already split on that). Walk it
    # looking for a single '-' between two word-char runs -- CPython's
    # wordsep_re keeps the hyphen with the left half ("goof-ball" ->
    # "goof-", "ball"). A run of 2+ hyphens (em-dash, "--", "---", ...) is
    # left intact as its own thing, never split on.
    out = []
    start = 0
    i = 0
    n = len(word)
    while i < n:
        if word[i] != "-":
            i += 1
            continue
        j = i
        while j < n and word[j] == "-":
            j += 1
        run_len = j - i
        if (
            run_len == 1
            and i > start
            and _is_word_char(word[i - 1])
            and j < n
            and _is_word_char(word[j])
        ):
            out.append(word[start : i + 1])
            start = i + 1
            i += 1
        else:
            i = j
    out.append(word[start:])
    return out


def _split_words_metal(text, break_on_hyphens):
    chunks = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in _whitespace:
            j = i
            while j < n and text[j] in _whitespace:
                j += 1
            chunks.append(text[i:j])
        else:
            j = i
            while j < n and text[j] not in _whitespace:
                j += 1
            word = text[i:j]
            if break_on_hyphens:
                chunks.extend(_split_hyphens_metal(word))
            else:
                chunks.append(word)
        i = j
    return chunks


class TextWrapper:
    """
    Object for wrapping/filling text.  The public interface consists of
    the wrap() and fill() methods; the other methods are just there for
    subclasses to override in order to tweak the default behaviour.
    If you want to completely replace the main wrapping algorithm,
    you'll probably have to override _wrap_chunks().

    Several instance attributes control various aspects of wrapping:
      width (default: 70)
        the maximum width of wrapped lines (unless break_long_words
        is false)
      initial_indent (default: "")
        string that will be prepended to the first line of wrapped
        output.  Counts towards the line's width.
      subsequent_indent (default: "")
        string that will be prepended to all lines save the first
        of wrapped output; also counts towards each line's width.
      expand_tabs (default: true)
        Expand tabs in input text to spaces before further processing.
        Each tab will become 0 .. 'tabsize' spaces, depending on its position
        in its line.  If false, each tab is treated as a single character.
      tabsize (default: 8)
        Expand tabs in input text to 0 .. 'tabsize' spaces, unless
        'expand_tabs' is false.
      replace_whitespace (default: true)
        Replace all whitespace characters in the input text by spaces
        after tab expansion.  Note that if expand_tabs is false and
        replace_whitespace is true, every tab will be converted to a
        single space!
      fix_sentence_endings (default: false)
        Ensure that sentence-ending punctuation is always followed
        by two spaces.  Off by default because the algorithm is
        (unavoidably) imperfect.
      break_long_words (default: true)
        Break words longer than 'width'.  If false, those words will not
        be broken, and some lines might be longer than 'width'.
      break_on_hyphens (default: true)
        Allow breaking hyphenated words. If true, wrapping will occur
        preferably on whitespaces and right after hyphens part of
        compound words.
      drop_whitespace (default: true)
        Drop leading and trailing whitespace from lines.
      max_lines (default: None)
        Truncate wrapped lines.
      placeholder (default: ' [...]')
        Append to the last line of truncated text.
    """

    # Metal patch: upstream's wordsep_re/wordsep_simple_re used
    # lookahead/lookbehind ((?=...), (?<=...)) to find hyphen/em-dash break
    # points -- re1.5 (this build's whole regex engine, extmod/modre.c +
    # lib/re1.5) is a Thompson NFA with no backtracking and structurally
    # cannot support those. _split() below replicates the same chunking
    # (whitespace runs, mid-word hyphen breaks, em-dash runs) with plain
    # character scanning instead of a regex -- see _split_words_metal()
    # near the bottom of this file. Close to CPython's textwrap on the
    # common cases; not guaranteed byte-identical on every corner case
    # upstream's regex covers (e.g. its [^0-9\W] "not-a-digit word char"
    # nuance around a hyphen).

    # XXX this is not locale- or charset-aware -- string.lowercase
    # is US-ASCII only (and therefore English-only)
    sentence_end_re = re.compile(
        r"[a-z]"  # lowercase letter
        r"[\.\!\?]"  # sentence-ending punct.
        r"[\"\']?"  # optional end-of-quote
        r"\Z"
    )  # end of chunk

    def __init__(
        self,
        width=70,
        initial_indent="",
        subsequent_indent="",
        expand_tabs=True,
        replace_whitespace=True,
        fix_sentence_endings=False,
        break_long_words=True,
        drop_whitespace=True,
        break_on_hyphens=True,
        tabsize=8,
        *,
        max_lines=None,
        placeholder=" [...]"
    ):
        self.width = width
        self.initial_indent = initial_indent
        self.subsequent_indent = subsequent_indent
        self.expand_tabs = expand_tabs
        self.replace_whitespace = replace_whitespace
        self.fix_sentence_endings = fix_sentence_endings
        self.break_long_words = break_long_words
        self.drop_whitespace = drop_whitespace
        self.break_on_hyphens = break_on_hyphens
        self.tabsize = tabsize
        self.max_lines = max_lines
        self.placeholder = placeholder

    # -- Private methods -----------------------------------------------
    # (possibly useful for subclasses to override)

    def _munge_whitespace(self, text):
        """_munge_whitespace(text : string) -> string

        Munge whitespace in text: expand tabs and convert all other
        whitespace characters to spaces.  Eg. " foo\tbar\n\nbaz"
        becomes " foo    bar  baz".
        """
        if self.expand_tabs:
            text = _expandtabs_metal(text, self.tabsize)
        if self.replace_whitespace:
            text = _translate_ws_metal(text, _whitespace, " ")
        return text

    def _split(self, text):
        """_split(text : string) -> [string]

        Split the text to wrap into indivisible chunks.  Chunks are
        not quite the same as words; see _wrap_chunks() for full
        details.  As an example, the text
          Look, goof-ball -- use the -b option!
        breaks into the following chunks:
          'Look,', ' ', 'goof-', 'ball', ' ', '--', ' ',
          'use', ' ', 'the', ' ', '-b', ' ', 'option!'
        if break_on_hyphens is True, or in:
          'Look,', ' ', 'goof-ball', ' ', '--', ' ',
          'use', ' ', 'the', ' ', '-b', ' ', option!'
        otherwise.
        """
        chunks = _split_words_metal(text, self.break_on_hyphens is True)
        chunks = [c for c in chunks if c]
        return chunks

    def _fix_sentence_endings(self, chunks):
        """_fix_sentence_endings(chunks : [string])

        Correct for sentence endings buried in 'chunks'.  Eg. when the
        original text contains "... foo.\nBar ...", munge_whitespace()
        and split() will convert that to [..., "foo.", " ", "Bar", ...]
        which has one too few spaces; this method simply changes the one
        space to two.
        """
        i = 0
        patsearch = self.sentence_end_re.search
        while i < len(chunks) - 1:
            if chunks[i + 1] == " " and patsearch(chunks[i]):
                chunks[i + 1] = "  "
                i += 2
            else:
                i += 1

    def _handle_long_word(self, reversed_chunks, cur_line, cur_len, width):
        """_handle_long_word(chunks : [string],
                             cur_line : [string],
                             cur_len : int, width : int)

        Handle a chunk of text (most likely a word, not whitespace) that
        is too long to fit in any line.
        """
        # Figure out when indent is larger than the specified width, and make
        # sure at least one character is stripped off on every pass
        if width < 1:
            space_left = 1
        else:
            space_left = width - cur_len

        # If we're allowed to break long words, then do so: put as much
        # of the next chunk onto the current line as will fit.
        if self.break_long_words:
            cur_line.append(reversed_chunks[-1][:space_left])
            reversed_chunks[-1] = reversed_chunks[-1][space_left:]

        # Otherwise, we have to preserve the long word intact.  Only add
        # it to the current line if there's nothing already there --
        # that minimizes how much we violate the width constraint.
        elif not cur_line:
            cur_line.append(reversed_chunks.pop())

        # If we're not allowed to break long words, and there's already
        # text on the current line, do nothing.  Next time through the
        # main loop of _wrap_chunks(), we'll wind up here again, but
        # cur_len will be zero, so the next line will be entirely
        # devoted to the long word that we can't handle right now.

    def _wrap_chunks(self, chunks):
        """_wrap_chunks(chunks : [string]) -> [string]

        Wrap a sequence of text chunks and return a list of lines of
        length 'self.width' or less.  (If 'break_long_words' is false,
        some lines may be longer than this.)  Chunks correspond roughly
        to words and the whitespace between them: each chunk is
        indivisible (modulo 'break_long_words'), but a line break can
        come between any two chunks.  Chunks should not have internal
        whitespace; ie. a chunk is either all whitespace or a "word".
        Whitespace chunks will be removed from the beginning and end of
        lines, but apart from that whitespace is preserved.
        """
        lines = []
        if self.width <= 0:
            raise ValueError("invalid width %r (must be > 0)" % self.width)
        if self.max_lines is not None:
            if self.max_lines > 1:
                indent = self.subsequent_indent
            else:
                indent = self.initial_indent
            if len(indent) + len(self.placeholder.lstrip()) > self.width:
                raise ValueError("placeholder too large for max width")

        # Arrange in reverse order so items can be efficiently popped
        # from a stack of chucks.
        chunks.reverse()

        while chunks:
            # Start the list of chunks that will make up the current line.
            # cur_len is just the length of all the chunks in cur_line.
            cur_line = []
            cur_len = 0

            # Figure out which static string will prefix this line.
            if lines:
                indent = self.subsequent_indent
            else:
                indent = self.initial_indent

            # Maximum width for this line.
            width = self.width - len(indent)

            # First chunk on line is whitespace -- drop it, unless this
            # is the very beginning of the text (ie. no lines started yet).
            if self.drop_whitespace and chunks[-1].strip() == "" and lines:
                del chunks[-1]

            while chunks:
                l = len(chunks[-1])

                # Can at least squeeze this chunk onto the current line.
                if cur_len + l <= width:
                    cur_line.append(chunks.pop())
                    cur_len += l

                # Nope, this line is full.
                else:
                    break

            # The current line is full, and the next chunk is too big to
            # fit on *any* line (not just this one).
            if chunks and len(chunks[-1]) > width:
                self._handle_long_word(chunks, cur_line, cur_len, width)
                cur_len = sum(map(len, cur_line))

            # If the last chunk on this line is all whitespace, drop it.
            if self.drop_whitespace and cur_line and cur_line[-1].strip() == "":
                cur_len -= len(cur_line[-1])
                del cur_line[-1]

            if cur_line:
                if (
                    self.max_lines is None
                    or len(lines) + 1 < self.max_lines
                    or (
                        not chunks
                        or self.drop_whitespace
                        and len(chunks) == 1
                        and not chunks[0].strip()
                    )
                    and cur_len <= width
                ):
                    # Convert current line back to a string and store it in
                    # list of all lines (return value).
                    lines.append(indent + "".join(cur_line))
                else:
                    while cur_line:
                        if cur_line[-1].strip() and cur_len + len(self.placeholder) <= width:
                            cur_line.append(self.placeholder)
                            lines.append(indent + "".join(cur_line))
                            break
                        cur_len -= len(cur_line[-1])
                        del cur_line[-1]
                    else:
                        if lines:
                            prev_line = lines[-1].rstrip()
                            if len(prev_line) + len(self.placeholder) <= self.width:
                                lines[-1] = prev_line + self.placeholder
                                break
                        lines.append(indent + self.placeholder.lstrip())
                    break

        return lines

    def _split_chunks(self, text):
        text = self._munge_whitespace(text)
        return self._split(text)

    # -- Public interface ----------------------------------------------

    def wrap(self, text):
        """wrap(text : string) -> [string]

        Reformat the single paragraph in 'text' so it fits in lines of
        no more than 'self.width' columns, and return a list of wrapped
        lines.  Tabs in 'text' are expanded with string.expandtabs(),
        and all other whitespace characters (including newline) are
        converted to space.
        """
        chunks = self._split_chunks(text)
        if self.fix_sentence_endings:
            self._fix_sentence_endings(chunks)
        return self._wrap_chunks(chunks)

    def fill(self, text):
        """fill(text : string) -> string

        Reformat the single paragraph in 'text' to fit in lines of no
        more than 'self.width' columns, and return a new string
        containing the entire wrapped paragraph.
        """
        return "\n".join(self.wrap(text))


# -- Convenience interface ---------------------------------------------


def wrap(text, width=70, **kwargs):
    """Wrap a single paragraph of text, returning a list of wrapped lines.

    Reformat the single paragraph in 'text' so it fits in lines of no
    more than 'width' columns, and return a list of wrapped lines.  By
    default, tabs in 'text' are expanded with string.expandtabs(), and
    all other whitespace characters (including newline) are converted to
    space.  See TextWrapper class for available keyword args to customize
    wrapping behaviour.
    """
    w = TextWrapper(width=width, **kwargs)
    return w.wrap(text)


def fill(text, width=70, **kwargs):
    """Fill a single paragraph of text, returning a new string.

    Reformat the single paragraph in 'text' to fit in lines of no more
    than 'width' columns, and return a new string containing the entire
    wrapped paragraph.  As with wrap(), tabs are expanded and other
    whitespace characters converted to space.  See TextWrapper class for
    available keyword args to customize wrapping behaviour.
    """
    w = TextWrapper(width=width, **kwargs)
    return w.fill(text)


def shorten(text, width, **kwargs):
    """Collapse and truncate the given text to fit in the given width.

    The text first has its whitespace collapsed.  If it then fits in
    the *width*, it is returned as is.  Otherwise, as many words
    as possible are joined and then the placeholder is appended::

        >>> textwrap.shorten("Hello  world!", width=12)
        'Hello world!'
        >>> textwrap.shorten("Hello  world!", width=11)
        'Hello [...]'
    """
    w = TextWrapper(width=width, max_lines=1, **kwargs)
    return w.fill(" ".join(text.strip().split()))


# -- Loosely related functionality -------------------------------------


def _leading_ws_metal(line):
    i = 0
    n = len(line)
    while i < n and line[i] in (" ", "\t"):
        i += 1
    return line[:i]


def dedent(text):
    """Remove any common leading whitespace from every line in `text`.

    This can be used to make triple-quoted strings line up with the left
    edge of the display, while still presenting them in the source code
    in indented form.

    Note that tabs and spaces are both treated as whitespace, but they
    are not equal: the lines "  hello" and "\thello" are
    considered to have no common leading whitespace.  (This behaviour is
    new in Python 2.5; older versions of this module incorrectly
    expanded tabs before searching for common leading whitespace.)

    Metal patch: upstream builds this on re.MULTILINE (^/$ matching at
    every line boundary, not just start/end of the whole string) -- this
    build's re.compile() accepts a flags argument but modre.c silently
    ignores it (no MICROPY_PY_RE_DEBUG here), so ^/$ would only ever
    anchor to the whole string, not each line. Plain line-by-line
    scanning sidesteps that entirely and needs no regex at all.
    """
    lines = text.split("\n")
    margin = None
    for line in lines:
        if line.strip() == "":
            continue
        indent = _leading_ws_metal(line)
        if margin is None:
            margin = indent
        elif indent.startswith(margin):
            pass
        elif margin.startswith(indent):
            margin = indent
        else:
            margin = ""
            break

    if margin:
        out = []
        mlen = len(margin)
        for line in lines:
            if line.startswith(margin):
                out.append(line[mlen:])
            elif line.strip() == "":
                out.append("")
            else:
                out.append(line)
        text = "\n".join(out)
    return text


def indent(text, prefix, predicate=None):
    """Adds 'prefix' to the beginning of selected lines in 'text'.

    If 'predicate' is provided, 'prefix' will only be added to the lines
    where 'predicate(line)' is True. If 'predicate' is not provided,
    it will default to adding 'prefix' to all non-empty lines that do not
    consist solely of whitespace characters.
    """
    if predicate is None:

        def _default_predicate(line):
            return line.strip()

        predicate = _default_predicate

    def prefixed_lines():
        for line in text.splitlines(True):
            yield (prefix + line if predicate(line) else line)

    return "".join(prefixed_lines())


if __name__ == "__main__":
    # print dedent("\tfoo\n\tbar")
    # print dedent("  \thello there\n  \t  how are you?")
    print(dedent("Hello there.\n  This is indented."))
