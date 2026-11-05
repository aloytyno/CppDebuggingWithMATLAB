function engineName = runPipelineDemo(launchCpp)
%RUNPIPELINEDEMO Prepare the shared MATLAB session for the C++ NR pipeline.
%   RUNPIPELINEDEMO launches the built executable in a separate console.
%   RUNPIPELINEDEMO(false) prepares MATLAB for VS Code F5 debugging instead.

arguments
    launchCpp (1,1) logical = true
end

%% Configure the didactic 5G NR numerology
carrier = nrCarrierConfig;
carrier.SubcarrierSpacing = 30;
carrier.NSizeGrid = 51;
ofdmInfo = nrOFDMInfo(carrier);

nSubcarriers = 12 * carrier.NSizeGrid;
nSymbols = 14;
nPorts = 2;
fftSize = ofdmInfo.Nfft;
cpLengths = ofdmInfo.CyclicPrefixLengths(1:nSymbols);
sampleRate = ofdmInfo.SampleRate;

% The C++ process retrieves these base-workspace values through the Engine API.
assignin("base", "nSubcarriers", nSubcarriers);
assignin("base", "nSymbols", nSymbols);
assignin("base", "nPorts", nPorts);
assignin("base", "fftSize", fftSize);
assignin("base", "cpLengths", cpLengths);
assignin("base", "sampleRate", sampleRate);
assignin("base", "ofdmGrid", complex(zeros(nSubcarriers, nSymbols, nPorts)));
assignin("base", "resourceMap", zeros(nSubcarriers, nSymbols, nPorts));

%% Create persistent figures
createPipelineFigures(nSubcarriers, nSymbols, nPorts);

%% Share this desktop session with the C++ Engine client

if ~matlab.engine.isEngineShared
    engineName = "nrOfdmCppDebug_" + string(randi([1, 1e9]));
    matlab.engine.shareEngine(engineName);
    assignin("base", "sharedEngineName", engineName);
else
    engineName = matlab.engine.engineName;
    assignin("base", "sharedEngineName", engineName);
    matlab.engine.engineName
end

fprintf("Shared MATLAB engine: %s\n", engineName);
fprintf("VS Code users: run the 'Debug C++ Pipeline' configuration and paste this name.\n");

if launchCpp
    configureEngineRuntimePath;
    executable = fullfile(pwd, "build", "RelWithDebInfo", "nr_ofdm_debug_demo.exe");
    if ~isfile(executable)
        error("Build the project first. Expected executable: %s", executable);
    end

    % START returns immediately, leaving this MATLAB desktop available to Engine calls.
    command = sprintf('start "NR OFDM C++ Pipeline" "%s" "%s"', ...
        executable, engineName);
    system(command);
end
end

function configureEngineRuntimePath
runtimeFolders = [ ...
    fullfile(matlabroot, "bin", "win64"), ...
    fullfile(matlabroot, "extern", "bin", "win64")];
setenv("PATH", strjoin([runtimeFolders, string(getenv("PATH"))], pathsep));
end

function createPipelineFigures(nSubcarriers, nSymbols, nPorts)
figureHandle = figure( ...
    Name="C++ Resource Grid Debugger", ...
    NumberTitle="off", ...
    Color="w");
tiledlayout(figureHandle, 1, nPorts, TileSpacing="compact", Padding="compact");

gridAxes = gobjects(1, nPorts);
gridImages = gobjects(1, nPorts);
for port = 1:nPorts
    gridAxes(port) = nexttile;
    gridImages(port) = imagesc(0:nSymbols-1, 0:nSubcarriers-1, ...
        zeros(nSubcarriers, nSymbols));
    axis xy;
    xlabel("OFDM symbol");
    ylabel("Subcarrier");
    title(sprintf("Port %d", port - 1));
end
colormap(figureHandle, [1 1 1; 0.20 0.45 0.75; 0.90 0.35 0.15; 0.15 0.65 0.35]);
colorbar(gridAxes(end), Ticks=0:3, ...
    TickLabels=["Empty", "SSB", "DMRS", "PDSCH"]);

spectrumFigure = figure( ...
    Name="C++ OFDM Spectrum Debugger", ...
    NumberTitle="off", ...
    Color="w");
spectrumAxes = axes(spectrumFigure);
hold(spectrumAxes, "on");
spectrumLines = plot(spectrumAxes, nan, nan, "-", nan, nan, "--", LineWidth=1.2);
xlabel(spectrumAxes, "Frequency (MHz)");
ylabel(spectrumAxes, "Power/frequency (dB/Hz)");
title(spectrumAxes, "Time-domain waveform spectrum");
legend(spectrumAxes, "Port 0", "Port 1", Location="best");
grid(spectrumAxes, "on");

assignin("base", "gridAxes", gridAxes);
assignin("base", "gridImages", gridImages);
assignin("base", "spectrumAxes", spectrumAxes);
assignin("base", "spectrumLines", spectrumLines);
end
