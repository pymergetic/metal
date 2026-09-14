#!/usr/bin/env python3
"""Relay a public TCP listener to a loopback-only backend."""

import selectors
import socket
import sys

listen_host, listen_port, target_host, target_port = sys.argv[1:5]
selector = selectors.DefaultSelector()
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


def accept(listener):
    client, _ = listener.accept()
    client.setblocking(False)
    try:
        upstream = socket.create_connection((target_host, int(target_port)), timeout=5)
    except OSError:
        client.close()
        return
    upstream.setblocking(False)
    peers[client] = upstream
    peers[upstream] = client
    selector.register(client, selectors.EVENT_READ)
    selector.register(upstream, selectors.EVENT_READ)


def relay(sock):
    peer = peers.get(sock)
    if peer is None:
        return
    try:
        data = sock.recv(65536)
    except BlockingIOError:
        return
    except OSError:
        close_pair(sock)
        return
    if not data:
        close_pair(sock)
        return
    view = memoryview(data)
    while view:
        try:
            sent = peer.send(view)
        except BlockingIOError:
            selector.select(0.001)
            continue
        except OSError:
            close_pair(sock)
            return
        view = view[sent:]


listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind((listen_host, int(listen_port)))
listener.listen(128)
listener.setblocking(False)
selector.register(listener, selectors.EVENT_READ)

try:
    while True:
        for key, _ in selector.select():
            if key.fileobj is listener:
                accept(listener)
            else:
                relay(key.fileobj)
finally:
    for sock in list(peers):
        close_pair(sock)
    listener.close()
