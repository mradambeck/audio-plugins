#include "ConvolutionVariant.h"

#include "BinaryData.h"

// ConvBase's half of the variant contract: which IRs this build ships. A branded variant's version
// of this file differs only in the table below - same includes, same shape, same function.
//
// These four are synthetic (tools/make_test_irs.py) and each exists to exercise a specific path
// through the shared engine, not to sound good. See the README.
namespace wildjag::conv
{

const ConvolutionVariant& variantConfig()
{
    // Function-local static: constructed on first use and outlives every processor instance, which
    // is what ConvolutionVariant.h's contract requires of the returned reference.
    static const ConvolutionVariant variant {
        "ConvBase",
        {
            { "Room (stereo, 48k)", "Native rate",
              BinaryData::roomstereo48k_flac, (size_t) BinaryData::roomstereo48k_flacSize },
            { "Hall (stereo, 44.1k)", "Resampled",
              BinaryData::hallstereo44k_flac, (size_t) BinaryData::hallstereo44k_flacSize },
            { "Plate (mono, 96k)", "Resampled",
              BinaryData::platemono96k_flac, (size_t) BinaryData::platemono96k_flacSize },
            { "Dirac (identity)", "Measurement",
              BinaryData::dirac48k_flac, (size_t) BinaryData::dirac48k_flacSize },
        },
        0
    };

    return variant;
}

} // namespace wildjag::conv
