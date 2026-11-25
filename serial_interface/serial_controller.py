"""
Serial Controller for BLE Framework

Handles serial communication with the nRF device for sending shell commands.
Used by data_processing/run_benchmark.py (SerialController, find_nordic_boards).
"""

import re
import subprocess
import time
import logging
import json
from typing import Any, Tuple
import serial

# Regex to strip ANSI escape codes
ANSI_ESCAPE_PATTERN = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]|\x1b\].*?\x07")

logger = logging.getLogger(__name__)


def find_nordic_boards():
    try:
        result = subprocess.run(
            ["nrfutil", "--json", "device", "list", "--traits", "jlink", "--skip-overhead"],
            capture_output=True,
            text=True,
            check=False
        )
    except FileNotFoundError as e:
        logger.info(f"Error: {e}")
        return []

    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError:
        logger.info("Error: nrfutil did not return valid JSON.")
        logger.info("stdout:", result.stdout)
        logger.info("stderr:", result.stderr)
        return []

    if result.returncode != 0:
        logger.info(f"Error: nrfutil exited with code {result.returncode}")
        logger.info("stdout:", result.stdout)
        logger.info("stderr:", result.stderr)
        return []

    boards = []

    for device in data.get("devices", []):
        device_family = device.get("devkit", {}).get("deviceFamily")
        serial_number = device.get("serialNumber")

        for port in device.get("serialPorts", []):
            if device_family == "NRF52_FAMILY":
            # https://docs.nordicsemi.com/r/bundle/ug_nrf52840_dk/page/ug/dk/vir_com_port.html
                if port.get("vcom") == 0:
                     boards.append({
                        "board": device_family,
                        "serial_number": serial_number,
                        "port": port["comName"]
            })
            if device_family == "NRF53_FAMILY":
            # https://nuttx.apache.org/docs/latest/platforms/arm/nrf53/boards/nrf5340-dk/index.html
                if port.get("vcom") == 0:
                    boards.append({
                    "board": device_family,
                    "serial_number": serial_number,
                    "port": port["comName"]
            })
            if device_family == "NRF54L_FAMILY":
            # https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/main/uart_configuration.html
            # " The VCOM1 is used for console/shell."
                if port.get("vcom") == 1:
                    boards.append({
                    "board": device_family,
                    "serial_number": serial_number,
                    "port": port["comName"]
            })


    return boards


class SerialController:
    """Controller for serial communication with the nRF device."""

    def __init__(
            self,
            baudrate: int = 115200,
            timeout: float = 10.0,
    ):
        self.serial = serial.Serial(
            baudrate = baudrate,
            timeout = timeout,
        )

    def connect(self) -> bool:
        """Open the serial connection and verify the shell is responding."""
        try:
            if self.serial.is_open:
                self.serial.close()
            self.serial.open()
            # Give the shell a moment to settle after (re)connecting
            time.sleep(0.3)

            print(f"Connected to {self.serial.port}")
            return True
        except Exception as e:
            print(f"[Error] Failed to connect: {e}. Maybe the port is used by another program?")
            return False


    def disconnect(self) -> None:
        """Close the serial connection."""
        if self.serial and self.serial.is_open:
            self.serial.close()
            print("Serial connection closed")
        self.serial = None

    def _strip_ansi(self, text: str) -> str:
        """Remove ANSI escape codes from text."""
        return ANSI_ESCAPE_PATTERN.sub("", text)

    def _is_noise_line(self, line: str, command: str) -> bool:
        """ Checks the return values of the shell.
            We wanna skip some that are not interesting for the user, e.g. empty lines,
            the commend itself which is echoed or log messages.
            These return values are skipped and not printed to the serial terminal.
        """
        # Empty lines
        if not line:
            return True
        # Command echo
        if command in line:
            return True
        # Zephyr log messages: [00:20:25.287,538] <inf> ...
        if re.match(r"^\[\d{2}:\d{2}:\d{2}\.\d+.*\]", line):
            # Bubble up warnings and errors so they appear in Python logs
            if "<err>" in line or "<wrn>" in line:
                print("DEVICE: %s", line)
            return True
        # Log level tags (no leading timestamp)
        if line.startswith("<inf>") or line.startswith("<dbg>"):
            return True
        if line.startswith("<wrn>") or line.startswith("<err>"):
            print("DEVICE: %s", line)
            return True
        return False

    def send_command(self, command: str) -> None:
        """Send a command without waiting for response."""
        if not self.serial or not self.serial.is_open:
            raise RuntimeError("Serial port not connected")

        self.serial.write(command.encode() + b"\r\n")
        self.serial.flush()

    def read_response(self, command: str = "", role: str = "", timeout: float = 2.0) -> Tuple[bool, Any]:
        """
        Read shell output until the shell prompt is received or timeout occurs.

        Args:
            command: The command that was sent
            role: Central or Peripheral to better distinguish who send the response
            timeout: Maximum time to wait for a valid response

        Returns:
            Tuple of (success, data) from the response
        """

        if not self.serial or not self.serial.is_open:
            raise RuntimeError("Serial port not connected")

        start_time = time.monotonic()
        response_lines = []
        buffer = ""
        line = ""

        if role == "central":
            response_lines.append("[Central]: ")
            role_name = "[Central]: "
        elif role == "peripheral":
            response_lines.append("[Peripheral]: ")
            role_name = "[Peripheral]: "
        else:
            role_name = ""

        while time.monotonic() - start_time < timeout:
            if self.serial.in_waiting == 0:
                time.sleep(0.01)
                continue

            # Read all currently available bytes
            raw = self.serial.read(self.serial.in_waiting)
            buffer += raw.decode("utf-8", errors="replace")
            buffer = self._strip_ansi(buffer)

            # Process complete lines
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()

                if line != "~$":
                    line = line.removeprefix("uart:~$").strip()

                print(role_name + line)

                if not self._is_noise_line(line, command):
                    response_lines.append(line)

                if "pair" in command:
                    if line.startswith("SC LTK:") or line.startswith("Legacy LTK:"):
                        return True, "\n".join(response_lines)

                    if "Connection failed" in line or "Pairing failed" in line:
                        return False, "\n".join(response_lines)

                # Normal Commands
                if (("init" in command and "Bluetooth connection callbacks registered." in line)
                or ("advertise start" in command and "Advertising successfully started" in line)
                or ("advertise stop" in command and "Advertising successfully stopped" in line)
                or ("scan stop" in command and "Scan successfully stopped" in line)
                or ("discover" in command and "CCC handle" in line)
                # "Pairing successfully cleared" (one address) or "Pairings successfully cleared" (all)
                or ("unpair" in command and "successfully cleared" in line)):
                    return True, "\n".join(response_lines)

                # fail fast instead of waiting for the timeout (e.g. -120: already initialized)
                if ("init" in command and "Bluetooth init failed" in line) or \
                   ("unpair" in command and "Failed to clear pairing" in line):
                    return False, "\n".join(response_lines)

                if "subscribe" in command:
                    if line.startswith("Subscribed"):
                        return True, "\n".join(response_lines)

                    if line.startswith("Subscribe failed"):
                        return False, "\n".join(response_lines)

            # Check whether the remaining buffer contains the shell prompt
            if ("init" not in command
                and "scan start" not in command
                and "scan stop" not in command
                and "advertise start" not in command
                and "advertise stop" not in command
                and "pair" not in command
                and "discover" not in command
                and "subscribe" not in command
                and "unpair" not in command
                and buffer.strip().endswith(":~$")):
                return True, "\n".join(response_lines)

        # scan start has no end marker, it collects devices until the timeout
        if "scan start" in command:
            return True, "\n".join(response_lines)

        print(f"[Error] Timeout waiting for response to command: {command}")
        return False, "\n".join(response_lines)


    def write_with_response(self, command: str, role: str, timeout: float = 2.0) -> Tuple[bool, Any]:
        """
        Send a command and return the response.

        Args:
            command: Shell command to send
            role: Central or Peripheral to better distinguish who send the response
            timeout: Maximum time to wait for a valid response

        Returns:
            Tuple of (success, data) from the response
        """
        self.send_command(command)
        return self.read_response(command, role, timeout)
