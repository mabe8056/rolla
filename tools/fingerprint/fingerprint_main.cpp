// rolla-fingerprint: see src/Tools/FingerprintTool.h.
#include "Tools/FingerprintTool.h"

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));
    return rolla::fptool::run (args);
}
