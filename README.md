# P!NG – Impulse Response Reverb (AU / VST3)

P!NG is an audio plugin you can use in Logic Pro (and other hosts) to add reverb by convolving your audio with impulse response (IR) files. You choose IRs from a folder on your Mac; the plugin shows them in a list and you pick one to use.

---

## What you need before building

1. **Xcode** (from the Mac App Store) – needed to compile the plugin. After installing, open Xcode once and accept the license if prompted.
2. **Xcode Command Line Tools** – so CMake can find the compiler. In Terminal run: `xcode-select --install` and complete the installer. If you already have Xcode, you may still need to run this once.
3. **CMake** – the build system. Install with Homebrew:
   - If you don’t have Homebrew: open Terminal and run:  
     `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
   - Then run: `brew install cmake`

4. **libsodium** – for licence verification. Run: `brew install libsodium`

---

## Build steps (Terminal)

1. Open **Terminal** (Applications → Utilities → Terminal).

2. Go to the project folder:
   ```bash
   cd "/Users/paulthomson/Cursor wip/Ping"
   ```

3. Create and enter the build folder:
   ```bash
   mkdir -p build && cd build
   ```

4. Generate the Xcode project (this will download JUCE the first time):
   ```bash
   cmake -G Xcode ..
   ```

5. Build the plugin:
   ```bash
   cmake --build . --config Release
   ```

When the build finishes, the plugin files are created inside `build`. The **AU** (for Logic) is a `.component` bundle.

---

## Installing the plugin so Logic can see it

- **AU (Logic Pro):**  
  Copy the built component to your user plugins folder:
  ```bash
  cp -R build/Ping_artefacts/Release/Audio\ Unit/Ping.component ~/Library/Audio/Plug-Ins/Components/
  ```
  If the path is different on your machine, look for `Ping.component` inside `build/` and copy it into `~/Library/Audio/Plug-Ins/Components/`.

- **VST3 (optional):**  
  Copy the built VST3 to:
  ```bash
  cp -R build/Ping_artefacts/Release/VST3/Ping.vst3 ~/Library/Audio/Plug-Ins/VST3/
  ```

Then **restart Logic** (or rescan plugins in Logic’s plugin manager). P!NG should appear as an Audio Unit effect.

---

## Where to put your impulse responses

The plugin looks for IR files in this folder:

**`~/Documents/P!NG/IRs`**

That means: **Documents** → create a folder named **P!NG** → inside it create a folder named **IRs**. Put your `.wav` or `.aiff` impulse response files in **IRs**.

- If the folder doesn’t exist, create it. The plugin will show an empty list until you add files.
- Supported formats: `.wav`, `.aiff`, `.aif`.
- After adding or removing files, click **Refresh** in the plugin to update the list.

---

## Using P!NG in Logic

1. Add an **Audio Effect** or **Aux** and choose **P!NG** (under Audio Unit → your manufacturer name).
2. In the plugin, pick an impulse response from the dropdown.
3. Use **Dry / Wet** to mix between dry (original) and wet (reverb).
4. **Predelay**, **Decay**, **Width**, **Reverse**, and the **EQ** knobs shape the reverb; save your project as usual and Logic will remember your settings.

---

## If something goes wrong

- **“No CMAKE_C_COMPILER could be found”**  
  Install the Xcode Command Line Tools: in Terminal run `xcode-select --install`. After it finishes, delete the `build` folder, then run the build steps again from step 3.

- **“cmake: command not found”**  
  Install CMake (see “What you need before building” above).

- **Build errors about JUCE or C++**  
  Make sure you ran `cmake -G Xcode ..` from inside the `build` folder and that Xcode is installed.

- **Logic doesn’t show P!NG**  
  Confirm the `.component` is in `~/Library/Audio/Plug-Ins/Components/`, then restart Logic and check Logic’s plugin manager for any validation errors.

- **No IRs in the list**  
  Create `~/Documents/P!NG/IRs`, put `.wav` or `.aiff` files there, then click **Refresh** in the plugin.

---

## Licence activation

P!NG requires activation with a name and serial number. On first use, a floating activation window appears. Enter the name and serial provided at purchase, then click **Activate**. The plugin stores the licence in its state and won't ask again.

---

## Generating serial numbers (developers)

The keygen tool in `Tools/` lets you create signed serials. **One-time setup:**

1. Install libsodium: `brew install libsodium`
2. Compile the keygen:
   ```bash
   cd Tools && g++ -std=c++17 -o keygen keygen.cpp -lsodium
   ```
3. Generate keys (do this once):
   ```bash
   ./keygen --generate-keys
   ```
4. Copy the printed `PUBLIC_KEY` array into `Source/LicenceVerifier.h` where indicated.
5. Store `private_key.bin` safely and **never commit it to git**.

**Issuing a serial:**
```bash
./keygen --name "Customer Name" --tier pro --expiry 2027-12-31
```
 tiers: `demo` | `standard` | `pro`. Use `--expiry 9999-12-31` for perpetual.
