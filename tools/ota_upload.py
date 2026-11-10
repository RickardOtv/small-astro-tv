#!/usr/bin/env python3
"""Upload firmware to a password-protected smalltv-mod over WiFi, in one pass.

curl --digest first sends the whole image without credentials, gets a 401,
then sends it again. That first refused upload has crashed the device, so this
script asks for the digest challenge with a small GET and sends the image once,
with the Authorization header already on it.

Usage: tools/ota_upload.py <host> <user> <password-file> <firmware.bin>
The password is read from a file so it never lands in shell history.
"""
import hashlib
import http.client
import os
import re
import sys


def md5(s):
    return hashlib.md5(s.encode()).hexdigest()


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    host, user, passfile, fw = sys.argv[1:5]
    with open(passfile) as f:
        pw = f.read().strip()

    # The challenge: any protected page answers 401 with realm, nonce and opaque.
    c = http.client.HTTPConnection(host, 80, timeout=20)
    c.request("GET", "/api/status")
    r = c.getresponse()
    r.read()
    f = dict(re.findall(r'(\w+)="([^"]*)"', r.getheader("WWW-Authenticate") or ""))
    if "nonce" not in f:
        sys.exit("no digest challenge (is a password set?)")

    uri, nc, cnonce = "/update", "00000001", os.urandom(8).hex()
    ha1 = md5(f"{user}:{f['realm']}:{pw}")
    resp = md5(f"{ha1}:{f['nonce']}:{nc}:{cnonce}:auth:{md5('POST:' + uri)}")
    auth = (f'Digest username="{user}", realm="{f["realm"]}", nonce="{f["nonce"]}", '
            f'uri="{uri}", algorithm=MD5, response="{resp}", opaque="{f.get("opaque", "")}", '
            f'qop=auth, nc={nc}, cnonce="{cnonce}"')
    del pw, ha1

    with open(fw, "rb") as fh:
        data = fh.read()
    b = "----ota" + os.urandom(6).hex()
    body = (f'--{b}\r\nContent-Disposition: form-data; name="update"; filename="firmware.bin"\r\n'
            f"Content-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{b}--\r\n".encode()
    c = http.client.HTTPConnection(host, 80, timeout=120)
    c.request("POST", uri, body,
              {"Authorization": auth, "Content-Type": f"multipart/form-data; boundary={b}"})
    r = c.getresponse()
    print(r.status, r.read()[:200].decode(errors="replace"))
    sys.exit(0 if r.status == 200 else 1)


if __name__ == "__main__":
    main()
