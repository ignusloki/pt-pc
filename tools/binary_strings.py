"""Null-terminated strings for binary-format inspection tools."""


def cstring(buf, offset):
    end = buf.index(b"\0", offset)
    return buf[offset:end].decode("ascii", "replace")


def latin1_cstring(data, offset):
    end = data.index(b"\0", offset)
    return data[offset:end].decode("latin-1")
