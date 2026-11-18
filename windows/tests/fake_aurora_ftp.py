#!/usr/bin/env python3
"""Minimal FTP server standing in for Aurora's, to test X360Remap for Windows
without a console. Python standard library only.

    python fake_aurora_ftp.py <root folder> [--port 2121] [--no-rename-over]
                              [--cut-new-after BYTES]

<root folder> holds the drives: <root>/Hdd1/X360RemapStudio/X360Remap.json ...

--no-rename-over        RNTO fails when the target exists (some servers do)
--cut-new-after BYTES   drop the data connection after BYTES bytes of any
                        "*.new" upload (network cable pulled mid-upload)
--reset-data            like Aurora's FtpDll: send the download / list data,
                        reset the data connection, and only then send "150"

Every command is printed, so a test can read what the client really did.
"""
import argparse
import os
import socket
import sys
import struct
import threading
import time


def log(text):
    print(text, flush=True)


class Session(threading.Thread):
    def __init__(self, conn, opts):
        super().__init__(daemon=True)
        self.conn = conn
        self.opts = opts
        self.pasv = None
        self.rename_from = None
        self.cwd = "/"

    def reply(self, text):
        self.conn.sendall((text + "\r\n").encode("utf-8"))

    def local(self, path):
        path = path.replace("\\", "/")
        if not path.startswith("/"):
            path = "/" + path
        full = os.path.normpath(os.path.join(self.opts.root, path.lstrip("/")))
        if not full.startswith(os.path.normpath(self.opts.root)):
            raise ValueError("outside root")
        return full

    def open_data(self):
        conn, _ = self.pasv.accept()
        self.pasv.close()
        self.pasv = None
        return conn

    def start_data(self):
        """Opens the data connection; "150" goes first, except with --reset-data."""
        if not self.opts.reset_data:
            self.reply("150 here it comes")
        return self.open_data()

    def close_data(self, data):
        if self.opts.reset_data:
            time.sleep(0.02)  # the data segment leaves before the reset
            data.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            data.close()
            time.sleep(0.05)  # the client sees data + reset before any reply
            self.reply("150 here it comes")
        else:
            data.close()

    def run(self):
        try:
            self.reply("220 FTP server ready (fake Aurora)")
            buf = b""
            while True:
                while b"\n" not in buf:
                    chunk = self.conn.recv(4096)
                    if not chunk:
                        return
                    buf += chunk
                line, buf = buf.split(b"\n", 1)
                line = line.decode("utf-8", "replace").rstrip("\r")
                cmd, _, arg = line.partition(" ")
                cmd = cmd.upper()
                log("< " + (cmd + " ****" if cmd == "PASS" else line))
                if not self.handle(cmd, arg):
                    return
        except (ConnectionError, OSError):
            pass
        finally:
            self.conn.close()

    def handle(self, cmd, arg):
        if cmd == "USER":
            self.reply("331 password required")
        elif cmd == "PASS":
            self.reply("230 logged in")
        elif cmd == "CWD":
            if os.path.isdir(self.local(arg)):
                self.cwd = arg
                self.reply("250 ok")
            else:
                self.reply("550 no such folder")
        elif cmd in ("TYPE", "NOOP"):
            self.reply("200 ok")
        elif cmd == "SYST":
            self.reply("215 UNIX Type: L8")
        elif cmd == "PWD":
            self.reply('257 "/"')
        elif cmd == "QUIT":
            self.reply("221 bye")
            return False
        elif cmd == "PASV":
            self.pasv = socket.socket()
            self.pasv.bind(("127.0.0.1", 0))
            self.pasv.listen(1)
            port = self.pasv.getsockname()[1]
            self.reply("227 Entering Passive Mode (127,0,0,1,%d,%d)" % (port // 256, port % 256))
        elif cmd in ("NLST", "LIST"):
            if self.opts.no_nlst and cmd == "NLST":
                self.pasv.close()
                self.reply("502 not implemented")
                return True
            path = self.local(arg or self.cwd)
            if not os.path.isdir(path):
                self.pasv.close()
                self.reply("550 no such folder")
                return True
            data = self.start_data()
            for name in sorted(os.listdir(path)):
                full = os.path.join(path, name)
                if cmd == "NLST":
                    data.sendall((name + "\r\n").encode("utf-8"))
                else:
                    kind = "d" if os.path.isdir(full) else "-"
                    size = 0 if os.path.isdir(full) else os.path.getsize(full)
                    data.sendall(("%srwxrwxrwx 1 root root %d Jan 01 2010 %s\r\n" % (kind, size, name)).encode("utf-8"))
            self.close_data(data)
            self.reply("226 done")
        elif cmd == "RETR":
            path = self.local(arg)
            if not os.path.isfile(path):
                self.pasv.close()
                self.reply("550 no such file")
                return True
            data = self.start_data()
            with open(path, "rb") as f:
                data.sendall(f.read())
            self.close_data(data)
            self.reply("226 File transferred")
        elif cmd == "SIZE":
            path = self.local(arg)
            if os.path.isfile(path):
                self.reply("213 %d" % os.path.getsize(path))
            else:
                self.reply("550 no such file")
        elif cmd == "STOR":
            path = self.local(arg)
            if not os.path.isdir(os.path.dirname(path)):
                self.pasv.close()
                self.reply("550 no such folder")
                return True
            self.reply("150 send it")
            data = self.open_data()
            cut = self.opts.cut_new_after if arg.endswith(".new") else None
            received = b""
            while True:
                chunk = data.recv(4096)
                if not chunk:
                    break
                received += chunk
                if cut is not None and len(received) >= cut:
                    # cable pulled: nothing reaches the disk, no final reply
                    data.close()
                    log("! upload of %s cut after %d bytes" % (arg, len(received)))
                    self.conn.close()
                    return False
            data.close()
            with open(path, "wb") as f:
                f.write(received)
            self.reply("226 stored")
        elif cmd == "RNFR":
            path = self.local(arg)
            if os.path.exists(path):
                self.rename_from = path
                self.reply("350 ready")
            else:
                self.reply("550 no such file")
        elif cmd == "RNTO":
            target = self.local(arg)
            if self.rename_from is None:
                self.reply("503 RNFR first")
            elif os.path.exists(target) and self.opts.no_rename_over:
                self.reply("550 target exists")
            else:
                os.replace(self.rename_from, target)
                self.reply("250 renamed")
            self.rename_from = None
        elif cmd == "MKD":
            path = self.local(arg)
            if os.path.isdir(path):
                self.reply("550 already exists")
            else:
                os.makedirs(path)
                self.reply('257 "%s" created' % arg)
        elif cmd == "DELE":
            path = self.local(arg)
            if os.path.isfile(path):
                os.remove(path)
                self.reply("250 deleted")
            else:
                self.reply("550 no such file")
        else:
            self.reply("502 not implemented")
        return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--no-rename-over", action="store_true")
    ap.add_argument("--no-nlst", action="store_true")
    ap.add_argument("--cut-new-after", type=int, default=None)
    ap.add_argument("--reset-data", action="store_true")
    ap.add_argument("--xbdm-port", type=int, default=None)
    ap.add_argument("--xbdm-die-after", type=int, default=None)
    ap.add_argument("--xbdm-reboot", action="store_true")
    opts = ap.parse_args()
    opts.root = os.path.abspath(opts.root)

    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", opts.port))
    server.listen(8)
    if opts.xbdm_port:
        def xbdm_session(conn):
            # a fixed answer per command: enough for XbdmConn and live_watch
            while True:
                line = conn.recv(256).decode("ascii", "replace").strip()
                if not line:
                    break
                log("xbdm < " + line)
                if line.lower().startswith("magicboot"):
                    # the console reboots: the plugin starts a new log
                    path = os.path.join(opts.root, "Hdd1", "X360RemapStudio", "X360Remap_plugin.log")
                    if os.path.isdir(os.path.dirname(path)):
                        with open(path, "w") as f:
                            f.write("[90ms] --- X360Remap plugin v1.3-test (fork of hiddriver360) starting, dashboard build 17559 ---\r\n")
                    break
                if line.startswith("threads"):
                    opts.xbdm_die_after = opts.xbdm_die_after and opts.xbdm_die_after - 1
                    if opts.xbdm_die_after == 0:
                        os._exit(0)     # the console froze: every socket drops
                    conn.sendall(b"202- multiline response follows\r\n7\r\n.\r\n")
                elif line.startswith("getmem"):
                    n = int(line.split("length=")[1], 16)
                    data = b"\x00\x00\x00\x03" if n == 4 else b"hi\n"
                    conn.sendall(b"202- memory data follows\r\n" + data.hex().encode() + b"\r\n.\r\n")
                elif line.startswith("getcontext"):
                    conn.sendall(b"202- multiline response follows\r\nMsr=0x1 R1=0x0\r\n.\r\n")
                elif line.startswith("notify"):
                    conn.sendall(b"205- now a notification channel\r\ndebugstr thread=7 lf string=X360Remap_LOG:hello\r\n"
                                 b"exception code=0xc0000005 thread=0x7 address=0x800bdf80 read=0x19 first\r\n"
                                 b"exception code=0xc0000005 thread=0x7 address=0x800bdf80 read=0x19 first\r\n"
                                 b"debugstr thread=7 lf string=after\r\n"
                                 + (b"execution rebooting\r\n" if opts.xbdm_reboot else b""))
                    conn.recv(16)
                    break
                else:
                    conn.sendall(b"200- OK\r\n")
            conn.close()

        def xbdm():
            x = socket.socket()
            x.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            x.bind(("127.0.0.1", opts.xbdm_port))
            x.listen(4)
            while True:
                conn, _ = x.accept()
                conn.sendall(b"201- connected\r\n")
                threading.Thread(target=xbdm_session, args=(conn,), daemon=True).start()
        threading.Thread(target=xbdm, daemon=True).start()
    log("fake Aurora FTP on 127.0.0.1:%d, root %s" % (opts.port, opts.root))
    try:
        while True:
            conn, _ = server.accept()
            Session(conn, opts).start()
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
