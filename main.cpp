#include "MatlabDataArray.hpp"
#include "MatlabEngine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Complex = std::complex<double>;
using Engine = matlab::engine::MATLABEngine;
using GridArray = matlab::data::TypedArray<Complex>;
using RealArray = matlab::data::TypedArray<double>;

constexpr double kInvSqrt2 = 0.7071067811865475;
constexpr double kTwoPi = 6.283185307179586;

template <typename ArrayType>
decltype(auto) linearElement(ArrayType& array, std::size_t linearIndex) {
    // MATLAB stores N-D arrays in column-major order. Iterators provide safe
    // flat access without the N-D index count required by TypedArray::operator[].
    return *(array.begin() + linearIndex);
}

struct PipelineConfig {
    std::size_t nSubcarriers;
    std::size_t nSymbols;
    std::size_t nPorts;
    std::size_t fftSize;
    std::vector<std::size_t> cpLengths;
    double sampleRate;
};

struct PipelineState {
    // ArrayFactory must be constructed before any MATLAB-compatible arrays.
    matlab::data::ArrayFactory factory;
    GridArray grid;
    RealArray resourceMap;

    explicit PipelineState(const PipelineConfig& config)
        : grid(factory.createArray<Complex>(
              {config.nSubcarriers, config.nSymbols, config.nPorts})),
          resourceMap(factory.createArray<double>(
              {config.nSubcarriers, config.nSymbols, config.nPorts})) {
        std::fill(grid.begin(), grid.end(), Complex{0.0, 0.0});
        std::fill(resourceMap.begin(), resourceMap.end(), 0.0);
    }

    std::size_t index(const PipelineConfig& config, std::size_t subcarrier,
                      std::size_t symbol, std::size_t port) const {
        return subcarrier + config.nSubcarriers *
            (symbol + config.nSymbols * port);
    }
};

std::u16string toUtf16(const std::string& text) {
    return std::u16string(text.begin(), text.end());
}

void waitForInspection(const std::string& message) {
    std::cout << "\n" << message << "\nPress Enter to continue...";
    std::string line;
    std::getline(std::cin, line);
}

double readScalar(Engine& matlabPtr,
                  const std::u16string& variableName) {
    // ===== MATLAB WORKSPACE FETCH: scalar configuration =====
    auto values = matlabPtr.getVariable(variableName);
    auto typedValues = matlab::data::TypedArray<double>(values);
    if (typedValues.getNumberOfElements() != 1) {
        throw std::runtime_error("Expected scalar MATLAB variable.");
    }
    return *typedValues.begin();
}

PipelineConfig readConfiguration(Engine& matlabPtr) {
    PipelineConfig config{};
    config.nSubcarriers = static_cast<std::size_t>(
        readScalar(matlabPtr, u"nSubcarriers"));
    config.nSymbols = static_cast<std::size_t>(
        readScalar(matlabPtr, u"nSymbols"));
    config.nPorts = static_cast<std::size_t>(readScalar(matlabPtr, u"nPorts"));
    config.fftSize = static_cast<std::size_t>(readScalar(matlabPtr, u"fftSize"));
    config.sampleRate = readScalar(matlabPtr, u"sampleRate");

    // ===== MATLAB WORKSPACE FETCH: vector configuration =====
    auto cpArray = matlabPtr.getVariable(u"cpLengths");
    auto cpValues = matlab::data::TypedArray<double>(cpArray);
    for (double value : cpValues) {
        config.cpLengths.push_back(static_cast<std::size_t>(value));
    }

    if (config.cpLengths.size() != config.nSymbols) {
        throw std::runtime_error("MATLAB CP configuration does not match symbols.");
    }
    return config;
}

void validateInitialArrays(Engine& matlabPtr,
                           PipelineState& state,
                           const PipelineConfig& config) {
    // ===== MATLAB WORKSPACE FETCH: initial 3-D resource-grid arrays =====
    auto grid = matlabPtr.getVariable(u"ofdmGrid");
    auto map = matlabPtr.getVariable(u"resourceMap");
    auto expected = matlab::data::ArrayDimensions{
        config.nSubcarriers, config.nSymbols, config.nPorts};

    if (grid.getDimensions() != expected || map.getDimensions() != expected) {
        throw std::runtime_error("MATLAB resource-grid dimensions are unexpected.");
    }
    const auto matlabGrid = matlab::data::TypedArray<Complex>(grid);
    const auto matlabMap = matlab::data::TypedArray<double>(map);

    // getVariable returns an Engine-owned array. Copy it into the writable
    // ArrayFactory arrays before C++ maps resource elements in place.
    std::copy(matlabGrid.begin(), matlabGrid.end(), state.grid.begin());
    std::copy(matlabMap.begin(), matlabMap.end(), state.resourceMap.begin());
}

Complex qpskSymbol(std::uint32_t& state) {
    // A compact LFSR makes the transport bits repeatable while debugging.
    auto nextBit = [&state]() {
        const std::uint32_t feedback =
            ((state >> 0U) ^ (state >> 2U) ^ (state >> 3U) ^ (state >> 5U)) & 1U;
        state = (state >> 1U) | (feedback << 31U);
        return static_cast<double>(state & 1U);
    };

    const double inPhase = nextBit() == 0.0 ? -kInvSqrt2 : kInvSqrt2;
    const double quadrature = nextBit() == 0.0 ? -kInvSqrt2 : kInvSqrt2;
    return {inPhase, quadrature};
}

void runStage1_ControlMapping(PipelineState& state,
                              const PipelineConfig& config) {
    const std::size_t ssbWidth = 240;
    const std::size_t ssbSymbols = 4;
    const std::size_t ssbStart = (config.nSubcarriers - ssbWidth) / 2;

    for (std::size_t port = 0; port < config.nPorts; ++port) {
        for (std::size_t symbol = 0; symbol < ssbSymbols; ++symbol) {
            for (std::size_t subcarrier = ssbStart;
                 subcarrier < ssbStart + ssbWidth; ++subcarrier) {
                const auto element = state.index(config, subcarrier, symbol, port);
                linearElement(state.grid, element) = Complex{kInvSqrt2, kInvSqrt2};
                linearElement(state.resourceMap, element) = 1.0; // SSB-like control region
            }
        }

        for (const std::size_t symbol : {std::size_t{4}, std::size_t{11}}) {
            for (std::size_t subcarrier = port * 3; subcarrier < config.nSubcarriers;
                 subcarrier += 6) {
                const auto element = state.index(config, subcarrier, symbol, port);
                if (linearElement(state.resourceMap, element) == 0.0) {
                    linearElement(state.grid, element) = Complex{1.0, 0.0};
                    linearElement(state.resourceMap, element) = 2.0; // DMRS
                }
            }
        }
    }
}

void runStage2_DataMapping(PipelineState& state, const PipelineConfig& config) {
    std::uint32_t transportBits = 0x12345678U;
    for (std::size_t port = 0; port < config.nPorts; ++port) {
        for (std::size_t symbol = 0; symbol < config.nSymbols; ++symbol) {
            for (std::size_t subcarrier = 0; subcarrier < config.nSubcarriers;
                 ++subcarrier) {
                const auto element = state.index(config, subcarrier, symbol, port);
                if (linearElement(state.resourceMap, element) != 0.0) {
                    continue;
                }
                linearElement(state.grid, element) = qpskSymbol(transportBits);
                linearElement(state.resourceMap, element) = 3.0; // PDSCH-like data RE
            }
        }
    }
}

void ifftRadix2(std::vector<Complex>& values) {
    const std::size_t count = values.size();
    for (std::size_t index = 1, reversed = 0; index < count; ++index) {
        std::size_t bit = count >> 1U;
        for (; (reversed & bit) != 0; bit >>= 1U) {
            reversed ^= bit;
        }
        reversed ^= bit;
        if (index < reversed) {
            std::swap(values[index], values[reversed]);
        }
    }

    // Iterative radix-2 butterflies avoid a dependency on an external FFT library.
    for (std::size_t length = 2; length <= count; length <<= 1U) {
        const Complex root = std::polar(1.0, kTwoPi / static_cast<double>(length));
        for (std::size_t offset = 0; offset < count; offset += length) {
            Complex twiddle{1.0, 0.0};
            for (std::size_t half = 0; half < length / 2; ++half) {
                const Complex even = values[offset + half];
                const Complex odd = values[offset + half + length / 2] * twiddle;
                values[offset + half] = even + odd;
                values[offset + half + length / 2] = even - odd;
                twiddle *= root;
            }
        }
    }

    for (Complex& value : values) {
        value /= static_cast<double>(count);
    }
}

GridArray runStage3_OFDMModulation(PipelineState& state,
                                   const PipelineConfig& config) {
    std::size_t samplesPerPort = 0;
    for (const std::size_t cpLength : config.cpLengths) {
        samplesPerPort += config.fftSize + cpLength;
    }

    auto waveform = state.factory.createArray<Complex>({samplesPerPort, config.nPorts});
    std::fill(waveform.begin(), waveform.end(), Complex{0.0, 0.0});

    for (std::size_t port = 0; port < config.nPorts; ++port) {
        std::size_t outputOffset = 0;
        for (std::size_t symbol = 0; symbol < config.nSymbols; ++symbol) {
            std::vector<Complex> frequencyBins(config.fftSize, {0.0, 0.0});
            for (std::size_t subcarrier = 0; subcarrier < config.nSubcarriers;
                 ++subcarrier) {
                const int centeredIndex = static_cast<int>(subcarrier) -
                    static_cast<int>(config.nSubcarriers / 2);
                const std::size_t fftBin = centeredIndex >= 0
                    ? static_cast<std::size_t>(centeredIndex)
                    : static_cast<std::size_t>(
                        static_cast<int>(config.fftSize) + centeredIndex);
                frequencyBins[fftBin] = linearElement(
                    state.grid, state.index(config, subcarrier, symbol, port));
            }
            ifftRadix2(frequencyBins);

            const std::size_t cpLength = config.cpLengths[symbol];
            for (std::size_t sample = 0; sample < cpLength; ++sample) {
                linearElement(waveform, outputOffset + sample + samplesPerPort * port) =
                    frequencyBins[config.fftSize - cpLength + sample];
            }
            for (std::size_t sample = 0; sample < config.fftSize; ++sample) {
                linearElement(
                    waveform, outputOffset + cpLength + sample + samplesPerPort * port) =
                    frequencyBins[sample];
            }
            outputOffset += cpLength + config.fftSize;
        }
    }
    return waveform;
}

void handoffGridToMatlab(Engine& matlabPtr,
                         const PipelineState& state, const std::string& stageLabel) {
    // ===== MATLAB WORKSPACE PUBLISH: resource grid and allocation map =====
    // VS CODE BREAKPOINT: MATLAB_VISUALIZATION_HANDOFF_GRID
    // Inspect state.grid/resourceMap here, then step into setVariable/eval to see
    // the in-memory C++ to MATLAB transfer and figure refresh.
    matlab::data::ArrayFactory factory;
    matlabPtr.setVariable(u"ofdmGrid", state.grid);
    matlabPtr.setVariable(u"resourceMap", state.resourceMap);
    matlabPtr.setVariable(u"stageLabel", factory.createCharArray(stageLabel));
    matlabPtr.eval(u"updateGridFigure");
}

void handoffWaveformToMatlab(Engine& matlabPtr,
                             const GridArray& waveform) {
    // ===== MATLAB WORKSPACE PUBLISH: final time-domain IQ waveform =====
    // VS CODE BREAKPOINT: MATLAB_VISUALIZATION_HANDOFF_WAVEFORM
    // The final complex waveform stays in memory while MATLAB computes its PSD.
    matlabPtr.setVariable(u"timeWaveform", waveform);
    matlabPtr.eval(u"updateSpectrumFigure");
}

void runSelfTest() {
    PipelineConfig config{612, 14, 2, 1024, std::vector<std::size_t>(14, 72),
                          30.72e6};

    // Keep this test independent of a live MATLAB Engine session.
    const std::size_t elementCount =
        config.nSubcarriers * config.nSymbols * config.nPorts;
    std::vector<double> resourceMap(elementCount, 0.0);
    const auto index = [&config](std::size_t subcarrier, std::size_t symbol,
                                 std::size_t port) {
        return subcarrier + config.nSubcarriers *
            (symbol + config.nSymbols * port);
    };

    const std::size_t ssbStart = (config.nSubcarriers - 240) / 2;
    for (std::size_t port = 0; port < config.nPorts; ++port) {
        for (std::size_t symbol = 0; symbol < 4; ++symbol) {
            for (std::size_t subcarrier = ssbStart; subcarrier < ssbStart + 240;
                 ++subcarrier) {
                resourceMap[index(subcarrier, symbol, port)] = 1.0;
            }
        }
        for (const std::size_t symbol : {std::size_t{4}, std::size_t{11}}) {
            for (std::size_t subcarrier = port * 3; subcarrier < config.nSubcarriers;
                 subcarrier += 6) {
                resourceMap[index(subcarrier, symbol, port)] = 2.0;
            }
        }
    }

    const auto reserved = std::count_if(resourceMap.begin(), resourceMap.end(),
                                        [](double code) { return code != 0.0; });
    if (reserved == 0) {
        throw std::runtime_error("Stage 1 did not create reserved resource elements.");
    }

    std::replace(resourceMap.begin(), resourceMap.end(), 0.0, 3.0);
    if (std::any_of(resourceMap.begin(), resourceMap.end(),
                    [](double code) { return code == 0.0; })) {
        throw std::runtime_error("Stage 2 left unmapped resource elements.");
    }

    std::vector<Complex> waveform(8, {0.0, 0.0});
    waveform[1] = {1.0, 0.0};
    ifftRadix2(waveform);
    if (std::any_of(waveform.begin(), waveform.end(), [](const Complex& value) {
            return !std::isfinite(value.real()) || !std::isfinite(value.imag());
        })) {
        throw std::runtime_error("IFFT generated invalid IQ samples.");
    }
    std::cout << "Self-test passed.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            runSelfTest();
            return 0;
        }
        if (argc != 2) {
            std::cerr << "Usage: nr_ofdm_debug_demo <shared-matlab-engine-name>\n";
            return 1;
        }

        const auto matlabPtr = matlab::engine::connectMATLAB(toUtf16(argv[1]));
        const PipelineConfig config = readConfiguration(*matlabPtr);
        PipelineState state(config);
        validateInitialArrays(*matlabPtr, state, config);

        runStage1_ControlMapping(state, config);
        handoffGridToMatlab(*matlabPtr, state, "Stage 1: SSB and DMRS");
        waitForInspection("Stage 1 complete. Inspect the pilot placement in MATLAB.");

        runStage2_DataMapping(state, config);
        handoffGridToMatlab(*matlabPtr, state, "Stage 2: PDSCH mapping");
        waitForInspection("Stage 2 complete. Confirm that data avoids reserved REs.");

        const GridArray waveform = runStage3_OFDMModulation(state, config);
        handoffWaveformToMatlab(*matlabPtr, waveform);
        waitForInspection("Stage 3 complete. Inspect the two-port PSD in MATLAB.");

        std::cout << "\nPipeline complete.\n";
    } catch (const std::exception& exception) {
        std::cerr << "Pipeline failed: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
