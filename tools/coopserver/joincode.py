"""Join codes (co-op design notes, "Hosting and joining a session").

Eight characters from an alphabet without look-alikes (no 0/O, 1/I/L),
case-insensitive, shown as XXXX-XXXX; the dash is optional when typed.
The engine has the same rules (src/libs/net/joincode.cpp); keep them equal.
"""
import secrets

ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"  # 31 symbols: 31^8 ~ 8.5e11 codes
LENGTH = 8

assert len(set(ALPHABET)) == len(ALPHABET) == 31
assert not set("01OIL") & set(ALPHABET)


def generate():
    return "".join(secrets.choice(ALPHABET) for _ in range(LENGTH))


def normalize(text):
    """The canonical code for what a player typed, or None when it is not one.

    Case is ignored, and so are one optional dash and surrounding spaces.
    Nothing is "corrected": a 0 or an O is simply not a code.
    """
    if not isinstance(text, str) or len(text) > 16:
        return None
    s = text.strip().upper()
    if s.count("-") > 1:
        return None
    s = s.replace("-", "")
    if len(s) != LENGTH or any(c not in ALPHABET for c in s):
        return None
    return s


def display(code):
    return code[:4] + "-" + code[4:]
