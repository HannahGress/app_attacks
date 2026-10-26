"""
Serial Controller for BLE Framework

Handles serial communication with the nRF device for sending shell commands.
"""

import re
import subprocess
import sys
import time
import logging
import json
from typing import Any, Tuple
from concurrent.futures import ThreadPoolExecutor
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
                if port.get("vcom") == 1:
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


def disconnect_system(serial_controller_c, serial_controller_p) -> None:
    """
    Disconnects the boards.
    Close the serial connection.
    Calls sys.exit()
    """
    serial_controller_c.write_with_response("bleframework unpair all", "central", 5.0)
    serial_controller_p.write_with_response("bleframework unpair all", "peripheral", 5.0)
    time.sleep(3)
    serial_controller_c.disconnect()
    serial_controller_p.disconnect()
    print("[INFO]: Dont't forget to reset the boards before rerunning the script.\n"
          "In case of failure, reflash the board (mostly when using the nRF54L15)")
    sys.exit()


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

    def read_response(self, command: str = "", role: str = "", timeout: float = 2.0, n_payload: str = "0") -> Tuple[bool, Any]:
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
                or ("unpair" in command and "Pairings successfully cleared" in line)):
                    return True, "\n".join(response_lines)

                if "subscribe" in command:
                    if line.startswith("Subscribed"):
                        return True, "\n".join(response_lines)

                    if line.startswith("Subscribe failed"):
                        return False, "\n".join(response_lines)

                if "time_results" in command:
                    if line.startswith("avg_DECRYPT") or line.startswith("avg_ENCRYPT"):
                        return True, "\n".join(response_lines)

            # Check whether the remaining buffer contains the shell prompt
            if ("init" not in command
                and "scan start" not in command
                and "scan stop" not in command
                and "advertise start" not in command
                and "advertise stop" not in command
                and "pair" not in command
                and "discover" not in command
                and "subscribe" not in command
                and "send_data" not in command
                and "time_results" not in command
                and "time_results_extended" not in command
                and "unpair" not in command
                and buffer.strip().endswith(":~$")):
                return True, "\n".join(response_lines)

        if "scan start" in command or "time_results_extended" in command:
            return True, "\n".join(response_lines)

        if "send_data" in command:
            if n_payload in line or "Notification":
                return True, "\n".join(response_lines)

        print(f"[Error] Timeout waiting for response to command: {command}")
        return False, "\n".join(response_lines)


    def write_with_response(self, command: str, role: str, timeout: float = 2.0, n_payload: str = "0") -> Tuple[bool, Any]:
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


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)

    # We instantiate our Central and Peripheral
    serial_controller_central = SerialController()
    serial_controller_peripheral = SerialController()

    user_input = ""
    address = ""

    print("Searching for nordic boards...")

    # we search for Nordic Boards
    while True:
        boards = find_nordic_boards()

        # We print out the list of found devices
        for i, board in enumerate(boards, start=1):
            print(
                f"Board {i}:\n"
                f"  Type: {board['board']}\n"
                f"  Serial: {board['serial_number']}\n"
                f"  Port: {board['port']}"
            )

        user_input = input("If all boards you were looking for are listed, press Enter else 'r' to rescan. Press 'q' to quit the program.").strip().lower()

        if user_input == "r":
            continue

        if user_input == "q":
            sys.exit()

        if len(boards) < 2:
            print("[Error] At least two boards are required.")
            continue

        # if the user presses Enter, we get an empty string which allows us to break out of the while loop
        # and continue with out program
        if user_input == "":
            break

        print("[Error] Invalid input. Press Enter or enter 'r'.")

    # If we have enough boards, we ask the user to specify the central and peripheral
    while True:
        try:
            central_num = int(input("Select board number for Central: "))
            peripheral_num = int(input("Select board number for Peripheral: "))

            if not (1 <= central_num <= len(boards)):
                print("[Error] Invalid Central board number.")
                continue

            if not (1 <= peripheral_num <= len(boards)):
                print("[Error] Invalid Peripheral board number.")
                continue

            if central_num == peripheral_num:
                print("[Error] Central and Peripheral must be different boards.")
                continue

            break

        except ValueError:
            print("[Error] Invalid input. Please enter a number.")

    central = boards[central_num - 1]
    peripheral = boards[peripheral_num - 1]

    # we assign the ports to our central and peripheral before connecting to them
    serial_controller_central.serial.port = central["port"]
    serial_controller_peripheral.serial.port = peripheral["port"]

    # we connect to the two devices vie the serial interface
    serial_controller_central.connect()
    serial_controller_peripheral.connect()

    # initialisation of the two boards
    serial_controller_central.write_with_response("bleframework init", "central")
    serial_controller_peripheral.write_with_response("bleframework init", "peripheral")

    while True:
        # peripheral starts advertising
        serial_controller_peripheral.write_with_response("bleframework advertise start", "peripheral")
        # central starts scanning; we save the response for further processing
        _, scan_response = serial_controller_central.write_with_response("bleframework scan start", "central")

        # scan results in the Nordic Semiconductor Serial Terminal look like the following
        # each device is listed twice, the information of the first entry are taken from the advertising data (AD evt type 0),
        # the infos of the second entry are taken from the scan response (AD evt type 4) and contain therefore the device name
        # [DEVICE]: 5B:48:04:82:98:25 (random), Name: <none>, AD evt type 0, AD data len 9, RSSI -31
        # [DEVICE]: 5B:48:04:82:98:25 (random), Name: BLE Framework, AD evt type 4, AD data len 15, RSSI -30

        # we search for devices named BLE Framework in our scan response; if the return value from the shell is None, we treat is as empty string
        matches = re.findall(r"\[DEVICE\]:\s+([0-9A-Fa-f:]{17}\s+\([^)]+\)),\s+Name:\s+BLE Framework,",
            scan_response or "")

        # We verify that we have at least one device named BLE Framework
        if len(matches) == 0:
            print("[Error] No BLE Framework device found.")

        # We raise an error if we have more than one BLE Framework device
        elif len(matches) > 1:
            print("[Error] Multiple BLE Framework devices found. Please keep only one in close proximity of the central device.")

        # if we found only one device, we save its address (e.g. 5B:48:04:82:98:25 (random))
        else:
            address = matches[0]
            print(f"Address found!: {address}")
            break

        user_input = input("Please enter 's' to scan again or 'q' to quit the program. ").strip().lower()

        if user_input == "s":
            # peripheral stops advertising
            serial_controller_peripheral.write_with_response("bleframework advertise stop", "peripheral")
            serial_controller_central.write_with_response("bleframework scan stop", "central")
            continue

        elif user_input == "q":
            disconnect_system(serial_controller_central, serial_controller_peripheral)

        else:
            print("[Error] Invalid input. Please enter 's' to scan again or 'q' to quit the program.")

    serial_controller_central.write_with_response("bleframework scan stop", "central")

    # adding energy consumption measurement if available
    user_input = input("Do you have a PPK II attached and want to additionally measure the energy consumption? Press 'y' for yes and 'n' for no.\n").strip().lower()

    while True:
        if user_input == "y":
            break
            # TODO Luana
        if user_input == "n":
            break
        if user_input == "q":
            disconnect_system(serial_controller_central, serial_controller_peripheral)
        else:
            print("[Error] Invalid input. Press 'y' for yes, 'n' for no and 'q' to quit the program.\n")

    # PAIRING
    while True:
        # Ask user if they want to pair with 7/16 bytes LTK or SC/Legacy Pairing
        user_input = input("Which pairing configuration should the connection have?\n"
                           "1. 16 Bytes LTK, Secure Connections\n"
                           "2. 7 Bytes LTK, Secure Connections\n"
                           "3. 16 Bytes LTK, Legacy Pairing\n"
                           "4. 7 Bytes LTK, Legacy Pairing\n").strip().lower()

        if user_input == "1":
            break
        if user_input == "2":
            serial_controller_central.write_with_response(f"bleframework knob true", "central")
            break
        if user_input == "3":
            serial_controller_central.write_with_response(f"bleframework scda true", "central")
            break
        if user_input == "4":
            serial_controller_central.write_with_response(f"bleframework knob true", "central")
            serial_controller_central.write_with_response(f"bleframework scda true", "central")
            break
        else:
            print("[Error] Invalid input. Please specify a number between 1 and 4.")
            user_input = input("If you want to quit the program, press 'q'")
            if user_input == "q":
                disconnect_system(serial_controller_central, serial_controller_peripheral)

    is_paired, pairing_response = (serial_controller_central.write_with_response(f"bleframework pair {address}","central", 15.0))

    if not is_paired:
        print("Pairing failed on Central")
        user_input = input("Press 'q' to quit the program and restart it again.")
        if user_input == "q":
            disconnect_system(serial_controller_central, serial_controller_peripheral)

    is_paired, peripheral_pairing_response = (serial_controller_peripheral.read_response("pair", "peripheral", 15.0))

    if not is_paired:
        print("Pairing failed on Peripheral")
        user_input = input("Press 'q' to quit the program and restart it again.")
        if user_input == "q":
            disconnect_system(serial_controller_central, serial_controller_peripheral)

    discovered, discover_response = serial_controller_central.write_with_response("bleframework discover", "central")

    if not discovered:
        user_input = input("Press 'q' to quit the program and restart it again.")
        if user_input == "q":
            disconnect_system(serial_controller_central, serial_controller_peripheral)

    subscribed, subscribe_response = serial_controller_central.write_with_response("bleframework subscribe", "central")

    if not subscribed:
        user_input = input("Press 'q' to quit the program and restart it again.")
        if user_input == "q":
            disconnect_system(serial_controller_central, serial_controller_peripheral)

    # enabling of benchmarking
    serial_controller_central.write_with_response("bleframework benchmark on", "central")
    serial_controller_peripheral.write_with_response("bleframework benchmark on", "peripheral")

    print("[INFO]: Everything is ready now. Write 'send_data <n> <payload size>' to start measuring.\n "
                 "'n' specifies the number of packets that you want to send per interation (max. 100). 'payload size' is limited to 244 bytes by the specification.\n"
                 "You can quit the program at any time by pressing 'q'.\n")

    while True:
        user_input = input("")
        if user_input.startswith("send_data"):

            n: str = user_input.split()[1]

            with ThreadPoolExecutor(max_workers=2) as executor:
                peripheral_future = executor.submit(serial_controller_peripheral.write_with_response,
                    "bleframework " + user_input, "peripheral", 5.0, n)

                central_future = executor.submit(serial_controller_central.read_response,
                    "send_data", "central", 5.0, n)

        if user_input == "q":
            disconnect_system(serial_controller_central, serial_controller_peripheral)
        if user_input == "r":
            serial_controller_central.write_with_response("bleframework time_results", "central", 5.0)
            serial_controller_peripheral.write_with_response("bleframework time_results", "peripheral", 5.0)
        if user_input == "x":
            serial_controller_central.write_with_response("bleframework time_results_extended", "central", 5.0)
            serial_controller_peripheral.write_with_response("bleframework time_results_extended", "peripheral", 5.0)
        if user_input == "c":
            serial_controller_central.write_with_response("bleframework reset_values", "central")
            serial_controller_peripheral.write_with_response("bleframework reset_values", "peripheral")
        print("[INFO]: To see min, max, avg and mean values of your measurement, press 'r'. To see sart, end and delta values of each iteration, press 'x'. To clear the result values, press 'c'. To quit, press 'q'. To make new measurements, write 'send_data <n> <payload size>'")