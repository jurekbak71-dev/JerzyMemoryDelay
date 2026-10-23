#pragma once
namespace JerzyAudio {
enum TapeDriveParamIds : unsigned int {
    kSatId = 300,
    kLevelId,
    kDryId,
    kGainModeId,
    kShiftId,
    kDriveBypassId,
    kDriveMeterId,
    kHPFCutoffId,
    kHPFResId,
    kLPFCutoffId,
    kLPFResId,
    kWowFlutterId,
    kInputMeterId,
    kSaturationMeterId,
    kPreampModeId,
    kPreampDriveId,
    kFlutterId,
    kTapeAgeId,
    kOptoAmountId,
    kOptoMakeupId,
    kOptoBypassId,
    kOptoMeterId
};
struct TapeDriveParams {
    double sat=0.35;
    double level=0.5;
    double dry=0.0;
    double gainMode=0.0;
    double shift=0.5;
    double bypass=0.0;
    double hpfCutoff=0.0;
    double hpfRes=0.018;
    double lpfCutoff=1.0;
    double lpfRes=0.018;
    double wowFlutter=0.0;
    double preampMode=0.0;
    double preampDrive=0.20;
    double flutter=0.0;
    double tapeAge=0.0;
    double optoAmount=0.35;
    double optoMakeup=0.5;
    double optoBypass=0.0;
};
// ID 311 is retained for host automation and sessions made before Wow/Flutter split.
constexpr unsigned int kWowId = kWowFlutterId;
}
