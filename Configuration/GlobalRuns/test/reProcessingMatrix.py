import optparse

usage="--list"
parser = optparse.OptionParser(usage)
parser.add_option("--GT")
parser.add_option("--era")
(options,args)=parser.parse_args()

def Era_8PDs():
    alcaMap={}
    alcaMap['']=''
    alcaMap['ZeroBias']='SiStripCalZeroBias'
    alcaMap['MinimumBias']='SiStripCalZeroBias+SiStripCalMinBias+TkAlMinBias+HcalCalIsoTrk'
    alcaMap['EG']='EcalCalElectron'
    alcaMap['Mu']='MuAlCalIsolatedMu+MuAlOverlaps+TkAlMuonIsolated+DtCalib'
    alcaMap['JetMETTau']='HcalCalDijets'
    #alcaMap['AlCaP0']='EcalCalPi0Calib+EcalCalEtaCalib'
    #alcaMap['AlCaPhiSymEcal']='EcalCalPhiSym+DQM'
    #alcaMap['HcalNZS']='HcalCalMinBias'
    #alcaMap['Cosmics']='TkAlBeamHalo+MuAlBeamHaloOverlaps+MuAlBeamHalo+TkAlCosmics0T+MuAlStandAloneCosmics+MuAlGlobalCosmics+MuAlCalIsolatedMu+HcalCalHOCosmics'
    return alcaMap

def Era_2PDs():
    alcaMap={}
    alcaMap['']=''
    alcaMap['ZeroBias']='SiStripCalZeroBias'
    alcaMap['MinimumBias']='SiStripCalMinBias+SiStripCalZeroBias+TkAlMinBias+TkAlMuonIsolated+MuAlCalIsolatedMu+MuAlOverlaps+HcalCalIsoTrk+HcalCalDijets+DtCalib+EcalCalElectron'
    return alcaMap

com='cmsDriver.py reco -s RAW2DIGI,L1Reco,RECO,DQM%s  --data --magField AutoFromDBCurrent --scenario pp --datatier RECO --eventcontent RECO,DQM --customise Configuration/GlobalRuns/reco_TLR_37X.py --cust_function customisePPData --no_exec --python_filename=rereco_%sCollision_37X.py --conditions %s'

import os

#GT='GR_R_37X_V5A::All'

alcaMap=globals()["Era_%ss"%(options.era)]()
for PD in alcaMap.keys():
    c=com
    if (PD==''):
        alca=''
        spec=''
        c=com+' --process reRECO'
    else:
        alca=',ALCA:%s'%(alcaMap[PD],)
        spec=PD+"_"
    os.system(c%(alca,spec,options.GT))


