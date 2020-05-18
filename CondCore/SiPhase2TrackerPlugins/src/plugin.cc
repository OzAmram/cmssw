#include "CondCore/ESSources/interface/registration_macros.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/SiPhase2OuterTrackerLorentzAngle.h"

#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "CondFormats/DataRecord/interface/SiStripCondDataRecords.h"

REGISTER_PLUGIN(TrackerDetToDTCELinkCablingMapRcd, TrackerDetToDTCELinkCablingMap);
REGISTER_PLUGIN(SiPhase2OuterTrackerLorentzAngleRcd, SiPhase2OuterTrackerLorentzAngle);
REGISTER_PLUGIN(SiPhase2OuterTrackerLorentzAngleSimRcd, SiPhase2OuterTrackerLorentzAngle);
