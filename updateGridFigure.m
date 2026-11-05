function updateGridFigure
%UPDATEGRIDFIGURE Refresh the persistent resource-grid images from base data.

resourceMap = evalin("base", "resourceMap");
gridImages = evalin("base", "gridImages");
gridAxes = evalin("base", "gridAxes");
stageLabel = evalin("base", "stageLabel");

for port = 1:size(resourceMap, 3)
    set(gridImages(port), CData=resourceMap(:, :, port).');
    title(gridAxes(port), sprintf("%s | Port %d", stageLabel, port - 1));
end
drawnow;
end
