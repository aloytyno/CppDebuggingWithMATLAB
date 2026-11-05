function runPipelineSelfTest
%RUNPIPELINESELFTEST Run the C++ pipeline validation with MATLAB runtime paths.

executable = fullfile(pwd, "build", "RelWithDebInfo", "nr_ofdm_debug_demo.exe");
if ~isfile(executable)
    error("Build the RelWithDebInfo executable first. Expected: %s", executable);
end

runtimeFolders = [ ...
    fullfile(matlabroot, "bin", "win64"), ...
    fullfile(matlabroot, "extern", "bin", "win64")];
setenv("PATH", strjoin([runtimeFolders, string(getenv("PATH"))], pathsep));

[status, output] = system(sprintf('"%s" --self-test 2>&1', executable));
fprintf("%s", output);
assert(status == 0, "The C++ pipeline self-test failed with status %d.", status);
end
