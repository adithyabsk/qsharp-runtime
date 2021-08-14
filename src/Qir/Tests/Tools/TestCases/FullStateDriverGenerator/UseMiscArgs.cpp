//----------------------------------------------------------------------------------------------------------------------
// <auto-generated />
// This code was generated by the Microsoft.Quantum.Qir.Runtime.Tools package.
// The purpose of this source code file is to provide an entry-point for executing a QIR program.
// It handles parsing of command line arguments, and it invokes an entry-point function exposed by the QIR program.
//----------------------------------------------------------------------------------------------------------------------

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "CLI11.hpp"

#include "QirRuntime.hpp"
#include "QirContext.hpp"

#include "SimFactory.hpp"

using namespace Microsoft::Quantum;
using namespace std;

// Auxiliary functions for interop with Q# Array type.
struct InteropArray
{
    int64_t Size;
    void* Data;

    InteropArray(int64_t size, void* data) : Size(size), Data(data)
    {
    }
};

template<typename T>
unique_ptr<InteropArray> CreateInteropArray(vector<T>& v)
{
    unique_ptr<InteropArray> array(new InteropArray(v.size(), v.data()));
    return array;
}

template<typename S, typename D>
void TranslateVector(vector<S>& sourceVector, vector<D>& destinationVector, function<D(S&)> translationFunction)
{
    destinationVector.resize(sourceVector.size());
    transform(sourceVector.begin(), sourceVector.end(), destinationVector.begin(), translationFunction);
}

// Auxiliary functions for interop with Q# Range type.
using RangeTuple = tuple<int64_t, int64_t, int64_t>;
struct InteropRange
{
    int64_t Start;
    int64_t Step;
    int64_t End;

    InteropRange() : Start(0), Step(0), End(0)
    {
    }

    InteropRange(RangeTuple rangeTuple) : Start(get<0>(rangeTuple)), Step(get<1>(rangeTuple)), End(get<2>(rangeTuple))
    {
    }
};

unique_ptr<InteropRange> CreateInteropRange(RangeTuple rangeTuple)
{
    unique_ptr<InteropRange> range(new InteropRange(rangeTuple));
    return range;
}

InteropRange* TranslateRangeTupleToInteropRangePointer(RangeTuple& rangeTuple)
{
    InteropRange* range = new InteropRange(rangeTuple);
    return range;
}

// Auxiliary functions for interop with Q# Bool type.
const char InteropFalseAsChar = 0x0;
const char InteropTrueAsChar  = 0x1;
map<string, bool> BoolAsCharMap{{"0", InteropFalseAsChar},
                                {"false", InteropFalseAsChar},
                                {"1", InteropTrueAsChar},
                                {"true", InteropTrueAsChar}};

// Auxiliary functions for interop with Q# String type.
const char* TranslateStringToCharBuffer(string& s)
{
    return s.c_str();
}

extern "C" void UseMiscArgs(char BoolArg, InteropArray* IntegerArrayArg, InteropRange* RangeArg,
                            const char* StringArg); // QIR interop function.

int main(int argc, char* argv[])
{
    CLI::App app("QIR Standalone Entry Point");

    // Initialize runtime.
    unique_ptr<IRuntimeDriver> sim = CreateFullstateSimulator();
    QirContextScope qirctx(sim.get(), false /*trackAllocatedObjects*/);

    // Add the --simulation-output option.
    string simulationOutputFile;
    CLI::Option* simulationOutputFileOpt = app.add_option(
        "--simulation-output", simulationOutputFile, "File where the output produced during the simulation is written");

    // Add a command line option for each entry-point parameter.
    char BoolArgCli;
    BoolArgCli = InteropFalseAsChar;
    app.add_option("--BoolArg", BoolArgCli, "Option to provide a value for the BoolArg parameter")
        ->required()
        ->transform(CLI::CheckedTransformer(BoolAsCharMap, CLI::ignore_case));

    vector<int64_t> IntegerArrayArgCli;
    app.add_option("--IntegerArrayArg", IntegerArrayArgCli,
                   "Option to provide a value for the IntegerArrayArg parameter")
        ->required();

    RangeTuple RangeArgCli;
    app.add_option("--RangeArg", RangeArgCli, "Option to provide a value for the RangeArg parameter")->required();

    string StringArgCli;
    app.add_option("--StringArg", StringArgCli, "Option to provide a value for the StringArg parameter")->required();

    // After all the options have been added, parse arguments from the command line.
    CLI11_PARSE(app, argc, argv);

    // Cast parsed arguments to its interop types.
    char BoolArgInterop = BoolArgCli;

    unique_ptr<InteropArray> IntegerArrayArgUniquePtr = CreateInteropArray(IntegerArrayArgCli);
    InteropArray* IntegerArrayArgInterop              = IntegerArrayArgUniquePtr.get();

    InteropRange* RangeArgInterop = TranslateRangeTupleToInteropRangePointer(RangeArgCli);

    const char* StringArgInterop = TranslateStringToCharBuffer(StringArgCli);

    // Redirect the simulator output from std::cout if the --simulation-output option is present.
    ostream* simulatorOutputStream = &cout;
    ofstream simulationOutputFileStream;
    if (!simulationOutputFileOpt->empty())
    {
        simulationOutputFileStream.open(simulationOutputFile);
        SetOutputStream(simulationOutputFileStream);
        simulatorOutputStream = &simulationOutputFileStream;
    }

    // Execute the entry point operation.
    UseMiscArgs(BoolArgInterop, IntegerArrayArgInterop, RangeArgInterop, StringArgInterop);

    // Flush the output of the simulation.
    simulatorOutputStream->flush();
    if (simulationOutputFileStream.is_open())
    {
        simulationOutputFileStream.close();
    }

    return 0;
}
