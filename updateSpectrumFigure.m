function updateSpectrumFigure
%UPDATESPECTRUMFIGURE Update the persistent spectrum plot from C++ IQ samples.

timeWaveform = evalin("base", "timeWaveform");
sampleRate = evalin("base", "sampleRate");
spectrumLines = evalin("base", "spectrumLines");

for port = 1:size(timeWaveform, 2)
    [psd, frequency] = pwelch(timeWaveform(:, port), [], [], [], sampleRate, "centered");
    set(spectrumLines(port), XData=frequency / 1e6, ...
        YData=pow2db(psd));
end
drawnow;
end
