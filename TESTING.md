# Testing guide

This project currently contains a runnable heatmap demo, but no automated test
cases are registered with CTest. Use the manual smoke test below to check that
the program builds and opens its plot.

## Current check: heatmap demo smoke test

**Purpose:** Build the C++ program and confirm that Matplot++ can display the
hard-coded sample matrix.

**Does not test:** TIFF loading, weighted-average calculations, website input,
or automated assertions. Those features are not implemented in this demo yet.

### Windows (VS Code PowerShell terminal)

Open **Terminal > New Terminal** in VS Code. Make sure the prompt is in the
project folder on D: (the path should end in `Sunlit-Summit-Heatmap-Test`).
These commands use the Visual Studio-bundled CMake because it may not be on
PowerShell's PATH, and assume vcpkg is installed at `D:\dev\vcpkg`.

Configure the project once, or again if its CMake configuration changes:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -DCMAKE_TOOLCHAIN_FILE=D:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
```

Build the executable (repeat after editing `points_heatmap.cpp`):

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release
```

Run the demo:

```powershell
$env:Path = "C:\Program Files\gnuplot\bin;$env:Path"; .\build\Release\points_heatmap.exe
```

**Expected result:** The program opens a window titled `points heatmap` with
the sample values from `points_heatmap.cpp`. If the terminal displays
`Press ENTER to continue...`, press Enter to finish.

### GitHub Codespaces or Linux development container

The development container configures the `dev` CMake preset during setup. In
the terminal, build the executable with:

```sh
cmake --build --preset dev
```

The build confirms compilation, but the interactive plot may not be visible in
the browser-based Codespaces environment. Use a local desktop with a display to
visually check the plot.

## Adding tests as features are implemented

When adding an automated test:

1. Put the test source in a clearly named file or test directory, such as
   `tests/test_weighted_score.cpp`.
2. Register it with CMake/CTest and build it with the project.
3. Add an entry here describing what it verifies, its test file, the command
   used to run it, and the expected result.
4. Keep this guide in sync whenever a test is added, removed, renamed, or its
   run instructions change.

Once tests are registered with CTest, contributors can run all automated tests
with `ctest --test-dir build --output-on-failure`. If tests are grouped with
CTest labels, document the available labels here and show the corresponding
filtered commands, for example `ctest --test-dir build -L scoring`.

For day-to-day collaboration, teammates can add or edit test files and update
this guide directly in the shared project. Git branches are optional; agree on
which files each person is editing to avoid simultaneous edits to the same file.
