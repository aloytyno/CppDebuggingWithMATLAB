# C++ 5G NR Grid Debugging with MATLAB

This demo shows C++ developers how to stop a resource-allocation pipeline at
meaningful stages and inspect complex 5G NR-like data structures in the active
MATLAB desktop. C++ owns the mapping and OFDM modulation; MATLAB receives only
in-memory Engine API arrays for visualization.

## Demo Description

The C++ application models a compact, didactic 5G NR-like downlink allocation.
It builds a three-dimensional resource grid whose dimensions are subcarrier,
OFDM symbol, and antenna port. MATLAB configures the numerology and provides
persistent figures; C++ performs the signal-processing work and pauses after
each meaningful stage.

1. **Control and reference mapping** places an SSB-like control region and
   port-specific DMRS pilots. The grid heatmap makes pilot placement visible.
2. **Data mapping** fills remaining resource elements with deterministic QPSK
   symbols. The second heatmap confirms that data does not overwrite reserved
   elements.
3. **OFDM modulation** applies IFFT and cyclic-prefix insertion for each port.
   MATLAB plots the resulting time-domain waveform's power spectral density.

The aim is not a complete 3GPP PHY. It is a readable C++ debugging workflow:
halt at a complex internal array, publish it to MATLAB without an intermediate
file, and use a domain-appropriate visualization to validate the algorithm.

## Key Techniques

- **Shared MATLAB desktop**: MATLAB calls `matlab.engine.shareEngine`, allowing
  the executable to connect to the already-visible desktop session.
- **C++ Engine API**: `matlab::engine::connectMATLAB` obtains an Engine client
  from the shared-session name. `getVariable`, `setVariable`, and `eval`
  exchange data and request figure updates.
- **MATLAB Data API**: `matlab::data::TypedArray` represents complex resource
  grids and real allocation maps; `matlab::data::ArrayFactory` creates the
  time-domain IQ matrix and MATLAB strings.
- **Persistent MATLAB graphics**: C++ calls small MATLAB helper functions that
  update existing image and line objects, avoiding a new figure per stage.
- **Debugger-friendly boundaries**: `MATLAB_VISUALIZATION_HANDOFF_GRID` and
  `MATLAB_VISUALIZATION_HANDOFF_WAVEFORM` mark the exact C++ lines where data
  is published to MATLAB.

## MATLAB and C++ Integration Recipe

### 1. Include the Engine and Data API headers

```cpp
#include "MatlabDataArray.hpp"
#include "MatlabEngine.hpp"
```

Add the MATLAB include root, not a subfolder, to the compiler and IntelliSense:

```text
C:\Program Files\MATLAB\R2026b_full\extern\include
```

The included `.vscode/c_cpp_properties.json` supplies that path for VS Code.
For example, `#include "MatlabController/MatlabControllerType.hpp"` resolves
relative to this include root.

### 2. Link the C++ application to MATLAB

For a Microsoft Visual C++ build, link these import libraries:

```text
<matlabroot>\extern\lib\win64\microsoft\libMatlabEngine.lib
<matlabroot>\extern\lib\win64\microsoft\libMatlabDataArray.lib
```

`CMakeLists.txt` configures the include directory and both libraries. Set
`MATLAB_ROOT` at CMake configure time if MATLAB is installed elsewhere.

### 3. Start and share the MATLAB host session

MATLAB must remain open because it owns the figures and processes Engine calls.
Run:

```matlab
engineName = runPipelineDemo(false)
```

The launcher initializes variables in the base workspace, calls
`matlab.engine.shareEngine(engineName)`, and prints the name the C++ program
must use.

### 4. Connect from C++

Pass the shared-engine name as the executable's first argument and connect:

```cpp
const auto matlabPtr = matlab::engine::connectMATLAB(engineName);
```

The VS Code F5 configuration prompts for this name. The normal MATLAB launcher
passes it to the executable automatically.

### 5. Transfer and modify MATLAB-compatible arrays

Fetch workspace values with `getVariable`, validate their dimensions, and copy
them into C++-owned `ArrayFactory` arrays before editing:

```cpp
auto gridFromMatlab = matlabPtr->getVariable(u"ofdmGrid");
auto matlabGrid = matlab::data::TypedArray<std::complex<double>>(gridFromMatlab);
std::copy(matlabGrid.begin(), matlabGrid.end(), state.grid.begin());
```

`ArrayFactory` must be declared before any arrays it creates because C++ builds
class members in declaration order. For multidimensional MATLAB arrays, use
iterators for a calculated column-major linear index rather than
`typedArray[linearIndex]`; square-bracket indexing expects an index for each
MATLAB dimension.

### 6. Publish a visualization checkpoint

```cpp
matlabPtr->setVariable(u"ofdmGrid", state.grid);
matlabPtr->setVariable(u"resourceMap", state.resourceMap);
matlabPtr->eval(u"updateGridFigure");
```

The transfer uses the Engine API in memory. No MAT, binary, CSV, or text file
is used for intermediate data.

### 7. Make the Engine runtime discoverable

External C++ processes need both runtime folders on `PATH`:

```text
<matlabroot>\bin\win64
<matlabroot>\extern\bin\win64
```

The MATLAB launch scripts and VS Code launch configuration set these paths.
Use the `RelWithDebInfo` CMake configuration: it retains PDB debug symbols
without requiring the separate Microsoft debug C runtime.

## File Manifest

| File | Description |
|---|---|
| `main.cpp` | Three-stage C++ mapping/modulation pipeline and Engine handoffs |
| `runPipelineDemo.m` | MATLAB launcher, NR numerology setup, shared-session setup, figures |
| `runPipelineSelfTest.m` | MATLAB-hosted C++ self-test launcher |
| `updateGridFigure.m` | Persistent categorical two-port resource-grid view |
| `updateSpectrumFigure.m` | Persistent two-port time-domain PSD view |
| `CMakeLists.txt` | Visual Studio CMake build and CTest self-test |
| `.vscode/` | Build task and breakpoint-ready C++ debugger launch profile |

## Setup Instructions

1. Use MATLAB R2026b with 5G Toolbox and Signal Processing Toolbox.
2. Open a Visual Studio Developer PowerShell, then run:

   ```powershell
   & "C:\Program Files\MATLAB\R2026b_full\bin\win64\cmake\bin\cmake.exe" -S . -B build -G "Visual Studio 17 2022" -A x64
   & "C:\Program Files\MATLAB\R2026b_full\bin\win64\cmake\bin\cmake.exe" --build build --config RelWithDebInfo
   ```

3. Start MATLAB in this folder and run `runPipelineSelfTest`.
4. Run `runPipelineDemo` to launch the debug-symbol executable in a separate console,
   or use the VS Code debugging workflow.

MATLAB must be open when the executable runs. It supplies the Engine runtime
and keeps the visualization windows responsive.

`CMakeLists.txt` also registers the same executable as a CTest test for
systems with a complete Kitware CMake installation.

## VS Code Debugging Workflow

1. In MATLAB, run `engineName = runPipelineDemo(false)`. This prepares the
   resource grid and prints a shared-engine name without launching C++.
2. Open this folder in VS Code and press `F5` with **Debug C++ Pipeline**.
3. Paste the engine name from MATLAB when VS Code prompts for it.
4. Set breakpoints on the comments named:
   - `MATLAB_VISUALIZATION_HANDOFF_GRID`
   - `MATLAB_VISUALIZATION_HANDOFF_WAVEFORM`
5. Inspect `state.grid`, `state.resourceMap`, or `waveform`, then step over
   `setVariable` and `eval` to see precisely where data enters MATLAB and
   refreshes each figure.

The C++ console pauses after each visualization. Press Enter only after
inspecting the MATLAB figures.

## Presentation Flow

1. Run stage 1 and inspect the SSB-like block plus port-specific DMRS pilots.
2. Run stage 2 and verify QPSK PDSCH mapping fills only empty REs.
3. Run stage 3 and inspect the two-port time-domain PSD.

## Notes

The allocation is numerology-consistent, but intentionally didactic rather
than a full 3GPP PHY. It uses an SSB-like control region, deterministic QPSK,
and uncoded transport bits to keep the C++ logic readable.

No MAT, binary, CSV, or text file is created while the pipeline is running.
The C++ client exchanges `matlab::data::Array` objects directly with the shared
MATLAB process.
