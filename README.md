# BLE Testing Framework

## Usage

### Attacks
The BLE frameworks contains five still standards-compliant attacks on BLE for Peripheral (4) and Central (5) devices.
The attacks are:
- [BLE Injection‑free Attack](https://link.springer.com/article/10.1007/s12652-019-01502-z)
- [Key Negotiation Of Bluetooth (KNOB) Attack](https://dl.acm.org/doi/abs/10.1145/3394497)
- [Nino Man-In-The-Middle Attack](https://ieeexplore.ieee.org/document/4401672?denied=)
- Secure Connections Downgrade Attack
- [Secure Connections Only mode Downgrade Attack](https://www.usenix.org/conference/usenixsecurity20/presentation/zhang-yue) (Central only)

### Commands
At each use or reset, initialize the BLE module with `bleframework init`. To see the available commands, type `bleframework`.

The commands for each attack are listed below.

#### BLE Injection‑free Attack
_Attack on Peripheral:_

If the device advertises its presence continuously, the attack can be conducted automatically.
```
bleframework scan start
// wait until the device's BDA appears (the device must be very close!)
// then launch the attack
bleframework ifa <BDA (public|private)> <n> 
// n specifies with how many fake IDs (fake BDAs) should be paired to fill the device's bonding list
```

If the pairing must be initiated by the user
```
bleframework scan start
// wait until the device's BDA appears (the device must be very close!)
// then launch the attack
// ifa stage 1: initial pairing; pairing with the real ID (BDA); ID 0 
bleframework ifa1 <BDA (public|private)>
// call ifa stage 2 n times
// n specifies with how many fake IDs (fake BDAs) should be paired to fill the device's bonding list
bleframework ifa2 <BDA (public|private)>
// ifa stage 3: resetting to initial ID (ID 0)
bleframework ifa3
// ifa stage 4: connect to device to see if the bonding information from ID 0 are still stored
bleframework ifa4 <BDA (public|private)>
```
_Attack on Central:_

The attack on a Central device cannot be conducted automatically, since the connection and pairing is always initiated by the Central device and we are the Peripheral here

```
// ifa stage 1
// we advertise and our Central pairs with us
bleframework advertise start
// when connected
bleframework ifa1
// repeat stage 2.1 - 2.2 n times:
// ifa stage 2.1: resetting ID to new ID and start advertising
bleframework ifa2_1_p
// Central pairs with us
// ifa stage 2.2: disconnection and deletion of bonding data from the Central
bleframework ifa2_2_p 
// ifa stage 3: resetting to initial ID (ID 0) 
bleframework ifa3
// ifa stage 4: we advertise, the Central connects to us and we can see, if it has still stored our bonding data
bleframework advertise start
```

#### KNOB Attack
Setting key size to seven with `knob true` and back to 16 with `knob false`. You can also set arbitrary sizes with `knob [7|8|9|10|11|12|13|14|15|16]`.
The commands set the key size for both roles, Central and Peripheral. Therefore, only advertising or scanning and then pairing is necessary to launch the attack.  

#### Nino Man-In-The-Middle Attack
This attack is implemented by default, because it makes testing easier.

#### Secure Connections Downgrade Attack
Downgrading to Legacy Pairing with `scda true` or disable with `scda false`.
The commands set the key size for both roles, Central and Peripheral. Therefore, only advertising or scanning and then pairing is necessary to launch the attack.

#### Secure Connections Only mode Downgrade Attack
This attack is only applicable to Central devices.


## Installation or Modification
If you only want to use the framework, you can download the prebuild .hex files for the nRF53840 DK and dongle as well as the nRF54L15 DK.
If you want to make modifications tot he project, follow the steps below. I used CLion as an IDE. The instructions are written for Windows, but can be adapted to Linux and Mac.
The project consist of two repositories, a fork of the [Zephyr project](https://github.com/HannahGress/zephyr_attacks), and this repository. When setting up the project, both will be combined.  

1. In your IDE, import the project as "Project from Version Control"
2. Open cmd and install the required dependencies as described [here](https://docs.zephyrproject.org/latest/develop/getting_started/index.html#install-dependencies) under the menu point "Install dependencies"
3. Install west. This can be done as follows (mixture and modification from [here](https://docs.zephyrproject.org/latest/develop/getting_started/index.html#get-zephyr-and-install-python-dependencies)
   and [here](https://docs.nordicsemi.com/bundle/ncs-1.5.1/page/zephyr/guides/west/manifest.html#west-manifests)
   (Example 1.2: “Rolling release” Zephyr downstream))
   1. Navigate into the project folder, e.g. `D:\CLionWorkspaces\ble-framework`
   2. Create and activate a virtual environment
   3. Install `west` with `pip install west`
   4. Initialize `west` by running `west init -l app_attacks`
   5. Run `west update`
   6. Open the cmd as admin. Create a symlink for `zephyr\_attacks` (Windows: `mklink /D zephyr zephyr_attacks`)
   7. Return to cmd as normal user. Export a Zephyr `CMake`package. This allows `CMake` to automatically load boilerplate code required for building Zephyr applications. Command: `west zephyr-export`
   8. Install Python dependencies using west packages with `west packages pip --install`
4. Install Zephyr’s `scripts\requirements.txt` with `pip install -r zephyr_attacks\scripts\requirements.txt`
5. Install the Zephyr SDK by navigating into the `zephyr_attacks` folder and calling `west sdk install` ([source](https://docs.zephyrproject.org/latest/develop/getting_started/index.html#install-the-zephyr-sdk))
6. [Information only] To update the Zephyr project source code, run the following commands while being in the `zephyr_attacks` folder ([source](https://zephyr-docs.listenai.com/guides/beyond-GSG.html#keeping-zephyr-updated)):
   ```
   git pull
   west update
   ```
   If the location of `zephyr` (in our case `zephyr_attacks`) changes, you also need to export the `CMake` package again (`west zephyr-export`).
7. Change the remote repos with
    ```
    git remote add origin https://github.com/HannahGress/zephyr_attacks.git
    git remote add upstream https://github.com/zephyrproject-rtos/zephyr.git
    ```
    After that, origin = your fork, upstream = Zephyr main ([source, modified](https://docs.zephyrproject.org/latest/contribute/guidelines.html#contribution-workflow))
8. Maybe you need to connect your local repo to the real `main` branch on GitHub
   ```
   git fetch origin
   git checkout -b main origin/main
   ```
   Now main exists locally and tracks your GitHub fork’s branch. After running `git branch -a` you should see a branch called `remotes/origin/main`.
9. Switch to your IDE.
10. Navigate into the `app_attacks/src/CMakeLists.txt` file and comment in/out the respective line for your board
11. Right-click on the `CMakeLists.txt` file and select `Load CMake Project`.
12. Got to `File` $\rightarrow$ `Settings` $\rightarrow$ `Build, Execution, Deployment` and configure the `Toolchain`
    and `CMake` as described [here](https://docs.zephyrproject.org/latest/develop/tools/clion.html#configure-the-toolchain-and-cmake-profile). Set also a python interpreter under the menu point `Python Interpreter`.
13. The IDE should create the build files automatically. To build the project, run `Build` $\rightarrow$ `Build zephyr_final` or click the hammer icon
14. [Information only] In case your built project does not contain all changes you made, go to `Tools` $\rightarrow$ `CMake` $\rightarrow$ `Reset Cache and Reload Project`. Then build again.
15. To flash the built project to your DK or dongle, you can use [`west flash`](https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html#flashing-west-flash) or Nordic Semiconductor's [Programmer App](https://www.nordicsemi.com/Products/Development-tools/nRF-Programmer). The built .hex file is located under `app_attacks\cmake-build-debug\zephyr\zephyr.hex`
16. If you want to add debugging, follow [these steps](https://docs.zephyrproject.org/latest/develop/tools/clion.html#configure-zephyr-parameters-for-debug)