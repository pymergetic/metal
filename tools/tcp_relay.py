#!/usr/bin/env python3
"""Relay a public TCP listener to a loopback-only backend.

Both directions are non-blocking and buffered: an accept never blocks on the
upstream connect, and a slow reader on either side only grows that peer's
output buffer instead of stalling every other connection. The old version
connected and forwarded synchronously, so one slow firmware response wedged
the whole event loop — Chromium's parallel page load then hung for a minute
while the same fetch took well under a second direct.

Half-closes are forwarded: a client that shuts down its write side after a
request still gets the whole response, and a pair ends when both directions
have delivered their EOF, not at the first one.
"""

import selectors
import socket
import sys

listen_host, listen_port, target_host, target_port = sys.argv[1:5]
target = (target_host, int(target_port))
selector = selectors.DefaultSelector()


class Peer:
    """One socket's half of a pair."""

    __slots__ = ("other", "out", "connecting", "src_eof", "fin_sent")

    def __init__(self, other):
        self.other = other       # the socket across the relay
        self.out = bytearray()   # bytes waiting to be sent ON this socket
        self.connecting = False  # upstream connect still in flight
        self.src_eof = False     # the other socket read EOF: out gets no more
        self.fin_sent = False    # that EOF has been forwarded to this socket


peers = {}


def close_pair(sock):
    peer = peers.pop(sock, None)
    try:
        selector.unregister(sock)
    except Exception:
        pass
    sock.close()
    if peer is not None and peers.pop(peer, None) is sock:
        try:
            selector.unregister(peer)
        except Exception:
            pass
        peer.close()


def watch(sock, write):
    events = selectors.EVENT_READ | (selectors.EVENT_WRITE if write else 0)
    try:
        selector.modify(sock, events, None)
    except KeyError:
        selector.register(sock, events, None)


def forward_eof(sock):
    """Shut down the write side once the buffer is drained and the pair is done
    only when both directions have said their goodbyes."""
    peer = peers.get(sock)
    if peer is None or not peer.src_eof or peer.fin_sent or peer.out:
        return
    peer.fin_sent = True
    try:
        sock.shutdown(socket.SHUT_WR)
    except OSError:
        close_pair(sock)
        return
    other = peers.get(peer.other)
    if other is not None and other.fin_sent:
        close_pair(sock)


def flush(sock):
    """Send what is buffered; keep the write interest until it is gone."""
    peer = peers.get(sock)
    if peer is None or peer.connecting:
        return
    while peer.out:
        try:
            sent = sock.send(peer.out)
        except BlockingIOError:
            watch(sock, True)
            return
        except OSError:
            close_pair(sock)
            return
        if sent == 0:
            close_pair(sock)
            return
        del peer.out[:sent]
    watch(sock, False)
    forward_eof(sock)


def accept(listener):
    try:
        client, _ = listener.accept()
    except OSError:
        return
    client.setblocking(False)
    upstream = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    upstream.setblocking(False)
    peers[client] = Peer(upstream)
    peers[upstream] = Peer(client)
    peers[upstream].connecting = True
    try:
        upstream.connect(target)
    except BlockingIOError:
        pass
    except OSError:
        close_pair(client)
        return
    # Writability on the upstream reports the connect verdict; reads on the
    # client may already arrive and are buffered toward the connecting socket.
    watch(client, False)
    watch(upstream, True)


def connected(sock):
    peer = peers.get(sock)
    if peer is None:
        return
    if sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR) != 0:
        close_pair(sock)
        return
    peer.connecting = False
    watch(sock, False)
    flush(sock)  # the request bytes that arrived before the connect finished


def relay_read(sock):
    peer = peers.get(sock)
    if peer is None or peer.connecting:
        return
    try:
        data = sock.recv(65536)
    except BlockingIOError:
        return
    except OSError:
        close_pair(sock)
        return
    dest = peers.get(peer.other)
    if dest is None:
        close_pair(sock)
        return
    if not data:
        # This side is done speaking; whatever it sent still reaches the far
        # side first, and only then does its EOF.
        dest.src_eof = True
        watch(sock, False)
        flush(peer.other)
        forward_eof(peer.other)
        return
    dest.out += data
    flush(peer.other)


listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind((listen_host, int(listen_port)))
listener.listen(128)
listener.setblocking(False)
selector.register(listener, selectors.EVENT_READ, None)

try:
    while True:
        for key, events in selector.select():
            if key.fileobj is listener:
                accept(listener)
                continue
            sock = key.fileobj
            peer = peers.get(sock)
            if peer is None:
                continue
            if peer.connecting:
                if events & selectors.EVENT_WRITE:
                    connected(sock)
                continue
            if events & selectors.EVENT_WRITE:
                flush(sock)
            if peers.get(sock) is not None and events & selectors.EVENT_READ:
                relay_read(sock)
finally:
    for sock in list(peers):
        close_pair(sock)
    listener.close()
