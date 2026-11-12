"""Read-only Google mail TLS readiness probe; never authenticates or sends mail."""
import imaplib
import json
import smtplib
import ssl


def main():
    context = ssl.create_default_context()
    failures = 0
    for label, operation in (
        ("imap-implicit-tls", lambda: imap(context)),
        ("smtp-implicit-tls", lambda: smtp(context, False)),
        ("smtp-starttls", lambda: smtp(context, True)),
    ):
        try:
            print(json.dumps({"probe": label, "ok": True, **operation()}), flush=True)
        except (OSError, ssl.SSLError, imaplib.IMAP4.error, smtplib.SMTPException) as exc:
            failures += 1
            print(json.dumps({"probe": label, "ok": False, "error": str(exc)}), flush=True)
    return bool(failures)


def imap(context):
    with imaplib.IMAP4_SSL("imap.gmail.com", 993, ssl_context=context, timeout=15) as server:
        capabilities = [item.decode("ascii") if isinstance(item, bytes) else item for item in server.capabilities]
        return {"tls": server.sock.version(), "xoauth2_advertised": "AUTH=XOAUTH2" in capabilities}


def smtp(context, starttls):
    if starttls:
        server = smtplib.SMTP("smtp.gmail.com", 587, timeout=15)
    else:
        server = smtplib.SMTP_SSL("smtp.gmail.com", 465, context=context, timeout=15)
    with server:
        server.ehlo("smoothmail.invalid")
        if starttls:
            server.starttls(context=context)
            server.ehlo("smoothmail.invalid")
        return {"tls": server.sock.version(), "xoauth2_advertised": "XOAUTH2" in server.esmtp_features.get("auth", "").upper()}


if __name__ == "__main__":
    raise SystemExit(main())
