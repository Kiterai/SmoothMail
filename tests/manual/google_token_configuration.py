"""Diagnose a Desktop client's token request using only deliberately invalid data.

No real authorization code, password, client secret or token is read or printed.
"""
import argparse
import json
import urllib.error
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client-id", required=True)
    args = parser.parse_args()
    if not args.client_id.endswith(".apps.googleusercontent.com"):
        parser.error("Expected a Google OAuth client ID")
    body = urllib.parse.urlencode({
        "client_id": args.client_id,
        "grant_type": "authorization_code",
        "code": "smoothmail-deliberately-invalid-code",
        "code_verifier": "smoothmail-deliberately-invalid-verifier-for-configuration-check",
        "redirect_uri": "http://127.0.0.1:49152/",
    }).encode("ascii")
    request = urllib.request.Request("https://oauth2.googleapis.com/token", data=body,
                                     headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            # There must never be a successful exchange of these dummy values.
            print("UNEXPECTED_SUCCESS_RESPONSE_NOT_DISPLAYED")
            return 1
    except urllib.error.HTTPError as error:
        data = json.loads(error.read(131073))
        code = data.get("error")
        safe_codes = {"invalid_request", "invalid_client", "invalid_grant", "unauthorized_client"}
        print("TOKEN_CONFIGURATION_ERROR=" + (code if code in safe_codes else "other"))
        description = data.get("error_description", "")
        if description == "client_secret is missing.":
            print("CLIENT_SECRET_REQUIRED=true")
        else:
            print("CLIENT_SECRET_REQUIRED=not_confirmed")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
