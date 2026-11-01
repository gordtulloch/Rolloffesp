#!/usr/bin/env python3
"""Small command-line client for an ASCOM Alpaca Dome device."""

from __future__ import annotations

import argparse
import json
import sys
from itertools import count
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


SHUTTER_STATES = {
    0: "Open",
    1: "Closed",
    2: "Opening",
    3: "Closing",
    4: "Error",
}


class AlpacaError(Exception):
    """Raised when an Alpaca request or response is unsuccessful."""


class AlpacaDomeClient:
    def __init__(
        self,
        base_url: str,
        device_number: int,
        client_id: int,
        timeout: float,
    ) -> None:
        self.base_url = base_url.rstrip("/")
        self.device_number = device_number
        self.client_id = client_id
        self.timeout = timeout
        self._transaction_ids = count(1)

    @property
    def device_url(self) -> str:
        return f"{self.base_url}/api/v1/dome/{self.device_number}"

    def get(self, property_name: str) -> object:
        return self._request("GET", property_name)

    def put(self, method_name: str, value: object = None) -> object:
        return self._request("PUT", method_name, value=value)

    def _request(self, method: str, member: str, value: object = None) -> object:
        transaction_id = next(self._transaction_ids)
        url = f"{self.device_url}/{member}"
        headers = {"Accept": "application/json"}
        data = None
        fields = {
            "ClientID": str(self.client_id),
            "ClientTransactionID": str(transaction_id),
        }
        if method == "PUT":
            headers["Content-Type"] = "application/x-www-form-urlencoded"
            if value is not None:
                fields["Connected"] = str(value).lower()
            data = urlencode(fields).encode("ascii")
        else:
            url = f"{url}?{urlencode(fields)}"
        request = Request(url, data=data, headers=headers, method=method)

        try:
            with urlopen(request, timeout=self.timeout) as response:
                body = response.read()
        except HTTPError as exc:
            body = exc.read()
            detail = self._error_detail(body)
            raise AlpacaError(f"HTTP {exc.code} for {method} {url}: {detail}") from exc
        except (URLError, TimeoutError, OSError) as exc:
            raise AlpacaError(f"Could not reach {url}: {exc}") from exc

        try:
            payload = json.loads(body)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise AlpacaError(f"Invalid JSON response from {url}") from exc

        if not isinstance(payload, dict):
            raise AlpacaError(f"Invalid Alpaca response from {url}: expected a JSON object")

        error_number = payload.get("ErrorNumber", 0)
        if error_number != 0:
            error_message = payload.get("ErrorMessage") or "unspecified Alpaca error"
            raise AlpacaError(f"Alpaca error {error_number} for {member}: {error_message}")
        return payload.get("Value")

    @staticmethod
    def _error_detail(body: bytes) -> str:
        try:
            payload = json.loads(body)
        except (UnicodeDecodeError, json.JSONDecodeError):
            return body.decode("utf-8", errors="replace").strip() or "no response body"
        if isinstance(payload, dict):
            number = payload.get("ErrorNumber")
            message = payload.get("ErrorMessage")
            if number is not None or message:
                return f"Alpaca error {number}: {message or 'unspecified error'}"
        return str(payload)


def parse_shutter_state(value: object) -> str:
    if isinstance(value, bool) or not isinstance(value, int):
        raise AlpacaError(f"Invalid ShutterStatus value: {value!r}")
    try:
        return SHUTTER_STATES[value]
    except KeyError as exc:
        raise AlpacaError(f"Unknown ShutterStatus value: {value}") from exc


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Read and control a roof through its ASCOM Alpaca Dome interface."
    )
    parser.add_argument(
        "--base-url",
        default="http://rolloffesp.local:11111",
        help="Alpaca server base URL (default: %(default)s)",
    )
    parser.add_argument(
        "--device",
        type=int,
        default=0,
        help="Alpaca Dome device number (default: %(default)s)",
    )
    parser.add_argument(
        "--client-id",
        type=int,
        default=1,
        help="Alpaca ClientID (default: %(default)s)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Request timeout in seconds (default: %(default)s)",
    )
    parser.add_argument(
        "command",
        choices=("status", "connect", "disconnect", "open", "close", "stop"),
        help="Dome operation to execute",
    )
    return parser


def run(args: argparse.Namespace) -> int:
    if args.device < 0:
        raise AlpacaError("Device number must not be negative")
    if args.timeout <= 0:
        raise AlpacaError("Timeout must be greater than zero")

    client = AlpacaDomeClient(
        args.base_url,
        args.device,
        args.client_id,
        args.timeout,
    )

    if args.command == "status":
        connected = client.get("connected")
        if not isinstance(connected, bool):
            raise AlpacaError(f"Invalid Connected value: {connected!r}")
        print(f"Connected: {connected}")
        shutter_status = client.get("shutterstatus")
        print(f"Shutter status: {parse_shutter_state(shutter_status)} ({shutter_status})")
    elif args.command == "connect":
        client.put("connected", True)
        print("Alpaca Dome connected.")
    elif args.command == "disconnect":
        client.put("connected", False)
        print("Alpaca Dome disconnected.")
    else:
        method = {
            "open": "openshutter",
            "close": "closeshutter",
            "stop": "abortslew",
        }[args.command]
        client.put(method)
        print(f"Sent {method}.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = create_parser()
    args = parser.parse_args(argv)
    try:
        return run(args)
    except AlpacaError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
