#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Bounded SOCKS5 lab: every virtual destination maps to an owned loopback socket.

No DNS, public connections, Tor service, or arbitrary destination forwarding.
One thread and at most two 64-KiB buffers per accepted connection, maximum 24.
"""
import ipaddress
import select
import socket
import signal
import struct
import subprocess
import threading
import time


def exact(sock, length):
    result = b''
    while len(result) < length:
        part = sock.recv(length - len(result))
        if not part:
            raise ConnectionError('Incomplete lab proxy request')
        result += part
    return result


def cleanup_owned_nodes(nodes):
    """Reap only this driver's children, including failed CI test runs."""
    for node in nodes:
        process = node.process
        if process is None:
            continue
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        node.process = None
        node.running = False
        node.rpc_connected = False


def run_owned_test(test):
    """Always reap this driver's children, including on an external watchdog.

    Do not change the framework's CI-wide leave-dangling setting. A TERM handler
    makes the bounded local finally block run; normal SystemExit also reaches it.
    """
    def terminated(signum, _frame):
        raise SystemExit(128 + signum)

    previous = signal.signal(signal.SIGTERM, terminated)
    owned = []
    shutdown = getattr(test, 'shutdown', None)
    if shutdown is not None:
        def retain_before_shutdown():
            # Core clears its list before returning from shutdown. Retain
            # references so failed-run destructors cannot kill without wait().
            owned.extend(test.nodes)
            return shutdown()
        test.shutdown = retain_before_shutdown
    try:
        test.main()
    finally:
        signal.signal(signal.SIGTERM, previous)
        cleanup_owned_nodes(owned + list(test.nodes))
        if shutdown is not None:
            test.shutdown = shutdown
        thread = getattr(test, 'network_thread', None)
        if thread is not None and thread.is_alive():
            thread.close(timeout=10)


class PQLabProxy:
    def __init__(self, destinations, *, latency=(0, 0), bytes_per_second=0):
        self.destinations = dict(destinations)
        for host, _ in self.destinations.values():
            assert ipaddress.ip_address(host).is_loopback
        self.latency = latency
        self.rate = bytes_per_second
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(24)
        self.listener.settimeout(0.1)
        self.address = f'127.0.0.1:{self.listener.getsockname()[1]}'
        self.stopping = threading.Event()
        self.slots = threading.BoundedSemaphore(24)
        self.lock = threading.Lock()
        self.children = []
        self.sockets = set()
        self.events = []
        self.errors = []
        self.acceptor = threading.Thread(target=self.accept)

    def accept(self):
        while not self.stopping.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            if not self.slots.acquire(blocking=False):
                client.close()
                continue
            worker = threading.Thread(target=self.forward, args=(client,))
            with self.lock:
                self.children = [thread for thread in self.children if thread.is_alive()]
                self.children.append(worker)
            worker.start()

    def forward(self, client):
        server = None
        event = {'destination': None, 'bytes': [0, 0], 'started': time.monotonic()}
        try:
            client.settimeout(5)
            with self.lock:
                self.sockets.add(client)
            version, count = exact(client, 2)
            if version != 5 or 0 not in exact(client, count):
                raise ConnectionError('Lab requires SOCKS5 without credentials')
            client.sendall(b'\x05\x00')
            version, command, reserved, kind = exact(client, 4)
            if (version, command, reserved) != (5, 1, 0):
                raise ConnectionError('Invalid SOCKS lab command')
            if kind == 1:
                host = socket.inet_ntop(socket.AF_INET, exact(client, 4))
            elif kind == 4:
                host = socket.inet_ntop(socket.AF_INET6, exact(client, 16))
            elif kind == 3:
                host = exact(client, exact(client, 1)[0]).decode('ascii')
            else:
                raise ConnectionError('Invalid SOCKS lab address type')
            port = struct.unpack('!H', exact(client, 2))[0]
            event['destination'] = (host, port)
            if (host, port) not in self.destinations:
                client.sendall(b'\x05\x02\x00\x01' + bytes(6))
                return  # No destination outside the explicit owned map.
            server = socket.create_connection(self.destinations[(host, port)], timeout=3)
            with self.lock:
                self.sockets.add(server)
            client.sendall(b'\x05\x00\x00\x01' + bytes(6))
            endpoints = [client, server]
            buffers = [bytearray(), bytearray()]
            send_at = [0.0, 0.0]
            eof = [False,False]
            write_closed = [False,False]
            for sock in endpoints:
                sock.setblocking(False)
            while not self.stopping.is_set():
                now = time.monotonic()
                if now - event['started'] > 300:
                    raise TimeoutError('Finite lab proxy connection deadline exceeded')
                for n, sock in enumerate(endpoints):
                    if eof[1-n] and not buffers[n] and not write_closed[n]:
                        sock.shutdown(socket.SHUT_WR)
                        write_closed[n] = True
                if all(eof) and not any(buffers):
                    return
                reads = [sock for n, sock in enumerate(endpoints) if not eof[n] and len(buffers[1-n]) < 65536]
                writes = [sock for n, sock in enumerate(endpoints) if buffers[n] and now >= send_at[n]]
                readable, writable, _ = select.select(reads, writes, [], 0.01)
                for sock in readable:
                    n = endpoints.index(sock)
                    data = sock.recv(65536 - len(buffers[1-n]))
                    if not data:
                        eof[n] = True
                        continue
                    if not buffers[1-n]:
                        send_at[1-n] = max(send_at[1-n], time.monotonic() + self.latency[n])
                    buffers[1-n].extend(data)
                for sock in writable:
                    n = endpoints.index(sock)
                    sent = sock.send(buffers[n])
                    del buffers[n][:sent]
                    event['bytes'][1-n] += sent
                    if self.rate:
                        send_at[n] = time.monotonic() + sent / self.rate
        except OSError as error:
            event['socket_error'] = type(error).__name__  # Expected unavailable endpoints are observable.
        except Exception as error:
            self.errors.append(str(error))
        finally:
            event['seconds'] = time.monotonic() - event['started']
            self.events.append(event)
            for sock in (client, server):
                if sock:
                    with self.lock:
                        self.sockets.discard(sock)
                    sock.close()
            self.slots.release()

    def __enter__(self):
        self.acceptor.start()
        return self

    def __exit__(self, *_):
        self.stopping.set()
        self.acceptor.join(2)
        with self.lock:
            for sock in self.sockets:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        for thread in self.children:
            thread.join(5)
            assert not thread.is_alive(), 'Lab proxy left a worker running'
        self.listener.close()
        assert not self.acceptor.is_alive()
        assert not self.errors, self.errors
