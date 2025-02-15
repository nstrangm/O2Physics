// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file   flowSP.cxx
/// \author Noor Koster
/// \since  01/12/2024
/// \brief  task to evaluate flow with respect to spectator plane.

#include <CCDB/BasicCCDBManager.h>
#include <DataFormatsParameters/GRPObject.h>
#include <DataFormatsParameters/GRPMagField.h>
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>

#include "Framework/runDataProcessing.h"
#include "Framework/AnalysisTask.h"
#include "Framework/ASoAHelpers.h"
#include "Framework/RunningWorkflowInfo.h"
#include "Framework/HistogramRegistry.h"

#include "Common/DataModel/EventSelection.h"
#include "Common/Core/TrackSelection.h"
#include "Common/DataModel/TrackSelectionTables.h"
#include "Common/DataModel/Multiplicity.h"
#include "Common/DataModel/Centrality.h"
#include "Common/Core/RecoDecay.h"

#include "PWGCF/DataModel/SPTableZDC.h"
#include "GFWWeights.h"
#include "TF1.h"

using namespace o2;
using namespace o2::framework;
using namespace o2::framework::expressions;
// using namespace o2::analysis;

#define O2_DEFINE_CONFIGURABLE(NAME, TYPE, DEFAULT, HELP) Configurable<TYPE> NAME{#NAME, DEFAULT, HELP};

struct FlowSP {
  // QA Plots
  O2_DEFINE_CONFIGURABLE(cfgFillQAHistos, bool, true, "Fill histograms for event and track QA");
  // Centrality Estimators -> standard is FT0C
  O2_DEFINE_CONFIGURABLE(cfgFT0Cvariant1, bool, false, "Set centrality estimator to cfgFT0Cvariant1");
  O2_DEFINE_CONFIGURABLE(cfgFT0M, bool, false, "Set centrality estimator to cfgFT0M");
  O2_DEFINE_CONFIGURABLE(cfgFV0A, bool, false, "Set centrality estimator to cfgFV0A");
  O2_DEFINE_CONFIGURABLE(cfgNGlobal, bool, false, "Set centrality estimator to cfgNGlobal");
  // Standard selections
  O2_DEFINE_CONFIGURABLE(cfgDCAxy, float, 0.2, "Cut on DCA in the transverse direction (cm)");
  O2_DEFINE_CONFIGURABLE(cfgDCAz, float, 2, "Cut on DCA in the longitudinal direction (cm)");
  O2_DEFINE_CONFIGURABLE(cfgNcls, float, 70, "Cut on number of TPC clusters found");
  O2_DEFINE_CONFIGURABLE(cfgFshcls, float, 0.2, "Cut on fraction of shared TPC clusters found");
  O2_DEFINE_CONFIGURABLE(cfgPtmin, float, 0.2, "minimum pt (GeV/c)");
  O2_DEFINE_CONFIGURABLE(cfgPtmax, float, 10, "maximum pt (GeV/c)");
  O2_DEFINE_CONFIGURABLE(cfgEta, float, 0.8, "eta cut");
  O2_DEFINE_CONFIGURABLE(cfgVtxZ, float, 10, "vertex cut (cm)");
  O2_DEFINE_CONFIGURABLE(cfgMagField, float, 99999, "Configurable magnetic field;default CCDB will be queried");
  O2_DEFINE_CONFIGURABLE(cfgCentMin, float, 0, "Minimum cenrality for selected events");
  O2_DEFINE_CONFIGURABLE(cfgCentMax, float, 90, "Maximum cenrality for selected events");
  // NUA and NUE weights
  O2_DEFINE_CONFIGURABLE(cfgFillWeights, bool, true, "Fill NUA weights");
  O2_DEFINE_CONFIGURABLE(cfgFillWeightsPOS, bool, false, "Fill NUA weights only for positive charges");
  O2_DEFINE_CONFIGURABLE(cfgFillWeightsNEG, bool, false, "Fill NUA weights only for negative charges");
  O2_DEFINE_CONFIGURABLE(cfgAcceptance, std::string, "", "ccdb dir for NUA corrections");
  O2_DEFINE_CONFIGURABLE(cfgEfficiency, std::string, "", "ccdb dir for NUE corrections");
  // Additional track Selections
  O2_DEFINE_CONFIGURABLE(cfgUseAdditionalTrackCut, bool, true, "Bool to enable Additional Track Cut");
  O2_DEFINE_CONFIGURABLE(cfgDoubleTrackFunction, bool, true, "Include track cut at low pt");
  O2_DEFINE_CONFIGURABLE(cfgTrackCutSize, float, 0.06, "Spread of track cut");
  // Additional event selections
  O2_DEFINE_CONFIGURABLE(cfgUseAdditionalEventCut, bool, true, "Bool to enable Additional Event Cut");
  O2_DEFINE_CONFIGURABLE(cfgMaxOccupancy, int, 10000, "Maximum occupancy of selected events");
  O2_DEFINE_CONFIGURABLE(cfgNoSameBunchPileupCut, bool, true, "kNoSameBunchPileupCut");
  O2_DEFINE_CONFIGURABLE(cfgIsGoodZvtxFT0vsPV, bool, true, "kIsGoodZvtxFT0vsPV");
  O2_DEFINE_CONFIGURABLE(cfgNoCollInTimeRangeStandard, bool, true, "kNoCollInTimeRangeStandard");
  O2_DEFINE_CONFIGURABLE(cfgDoOccupancySel, bool, true, "Bool for event selection on detector occupancy");
  O2_DEFINE_CONFIGURABLE(cfgTVXinTRD, bool, false, "Use kTVXinTRD (reject TRD triggered events)");
  O2_DEFINE_CONFIGURABLE(cfgIsVertexITSTPC, bool, true, "Selects collisions with at least one ITS-TPC track");
  O2_DEFINE_CONFIGURABLE(cfgIsGoodITSLayersAll, bool, true, "Cut time intervals with dead ITS staves");
  // harmonics for v coefficients
  O2_DEFINE_CONFIGURABLE(cfgHarm, int, 1, "Flow harmonic n for ux and uy: (Cos(n*phi), Sin(n*phi))");
  O2_DEFINE_CONFIGURABLE(cfgHarmMixed, int, 2, "Flow harmonic n for ux and uy in mixed harmonics (MH): (Cos(n*phi), Sin(n*phi))");
  // settings for CCDB data
  O2_DEFINE_CONFIGURABLE(cfgLoadAverageQQ, bool, true, "Load average values for QQ (in centrality bins)");
  O2_DEFINE_CONFIGURABLE(cfgCCDBdir, std::string, "Users/c/ckoster/ZDC/LHC23_zzh_pass4_small/meanQQ", "ccdb dir for average QQ values in 1% centrality bins");
  O2_DEFINE_CONFIGURABLE(cfgLoadSPPlaneRes, bool, false, "Load ZDC spectator plane resolution");
  O2_DEFINE_CONFIGURABLE(cfgCCDBdir_SP, std::string, "Users/c/ckoster/ZDC/LHC23_zzh_pass4_small/SPPlaneRes", "ccdb dir for average event plane resolution in 1% centrality bins");
  // axis
  ConfigurableAxis axisDCAz{"axisDCAz", {200, -.5, .5}, "DCA_{z} (cm)"};
  ConfigurableAxis axisDCAxy{"axisDCAxy", {200, -.5, .5}, "DCA_{xy} (cm)"};
  ConfigurableAxis axisPhiMod = {"axisPhiMod", {100, 0, constants::math::PI / 9}, "fmod(#varphi,#pi/9)"};
  ConfigurableAxis axisPhi = {"axisPhi", {60, 0, constants::math::TwoPI}, "#varphi"};
  ConfigurableAxis axisEta = {"axisEta", {64, -1.8, 1.8}, "#eta"};
  ConfigurableAxis axisEtaVn = {"axisEtaVn", {8, -.8, .8}, "#eta"};
  ConfigurableAxis axisVx = {"axisVx", {40, -0.01, 0.01}, "v_{x}"};
  ConfigurableAxis axisVy = {"axisVy", {40, -0.01, 0.01}, "v_{y}"};
  ConfigurableAxis axisVz = {"axisVz", {40, -10, 10}, "v_{z}"};
  ConfigurableAxis axisCent = {"axisCent", {90, 0, 90}, "Centrality(%)"};
  ConfigurableAxis axisPhiPlane = {"axisPhiPlane", {100, -constants::math::PI, constants::math::PI}, "#Psi"};

  Filter collisionFilter = nabs(aod::collision::posZ) < cfgVtxZ;
  Filter trackFilter = nabs(aod::track::eta) < cfgEta && aod::track::pt > cfgPtmin&& aod::track::pt < cfgPtmax && ((requireGlobalTrackInFilter()) || (aod::track::isGlobalTrackSDD == (uint8_t) true)) && nabs(aod::track::dcaXY) < cfgDCAxy&& nabs(aod::track::dcaZ) < cfgDCAz;
  using UsedCollisions = soa::Filtered<soa::Join<aod::Collisions, aod::EvSels, aod::Mults, aod::CentFT0Cs, aod::CentFT0CVariant1s, aod::CentFT0Ms, aod::CentFV0As, aod::CentNGlobals, aod::SPTableZDC>>;
  using UsedTracks = soa::Filtered<soa::Join<aod::Tracks, aod::TracksExtra, aod::TrackSelection, aod::TracksDCA>>;

  //  Connect to ccdb
  Service<ccdb::BasicCCDBManager> ccdb;

  // struct to hold the correction histos/
  struct Config {
    std::vector<TH1D*> mEfficiency = {};
    std::vector<GFWWeights*> mAcceptance = {};
    bool correctionsLoaded = false;
    int lastRunNumber = 0;

    TProfile* hcorrQQ = nullptr;
    TProfile* hcorrQQx = nullptr;
    TProfile* hcorrQQy = nullptr;
    TProfile* hEvPlaneRes = nullptr;
    bool clQQ = false;
    bool clEvPlaneRes = false;

  } cfg;

  // define output objects
  OutputObj<GFWWeights> fWeights{GFWWeights("weights")};
  OutputObj<GFWWeights> fWeightsPOS{GFWWeights("weights_positive")};
  OutputObj<GFWWeights> fWeightsNEG{GFWWeights("weights_negative")};
  HistogramRegistry registry{"registry"};

  // Event selection cuts - Alex
  TF1* fPhiCutLow = nullptr;
  TF1* fPhiCutHigh = nullptr;
  TF1* fMultPVCutLow = nullptr;
  TF1* fMultPVCutHigh = nullptr;
  TF1* fMultCutLow = nullptr;
  TF1* fMultCutHigh = nullptr;
  TF1* fMultMultPVCut = nullptr;

  enum SelectionCriteria {
    evSel_FilteredEvent,
    evSel_sel8,
    evSel_occupancy,
    evSel_kTVXinTRD,
    evSel_kNoSameBunchPileup,
    evSel_kIsGoodZvtxFT0vsPV,
    evSel_kNoCollInTimeRangeStandard,
    evSel_kIsVertexITSTPC,
    evSel_MultCuts,
    evSel_CentCuts,
    evSel_kIsGoodITSLayersAll,
    evSel_isSelectedZDC,
    nEventSelections
  };

  enum TrackSelections {
    trackSel_FilteredTracks,
    trackSel_NCls,
    trackSel_FshCls,
    trackSel_TPCBoundary,
    trackSel_ZeroCharge,
    trackSel_ParticleWeights,
    nTrackSelections
  };

  enum ChargeType {
    kInclusive,
    kPositive,
    kNegative
  };

  enum FillType {
    kBefore,
    kAfter
  };

  static constexpr std::string_view Charge[] = {"incl/", "pos/", "neg/"};

  void init(InitContext const&)
  {
    ccdb->setURL("http://alice-ccdb.cern.ch");
    ccdb->setCaching(true);
    ccdb->setLocalObjectValidityChecking();

    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    ccdb->setCreatedNotAfter(now);

    std::vector<double> ptbinning = {0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2, 2.2, 2.4, 2.6, 2.8, 3, 3.5, 4, 5, 6, 8, 10};
    AxisSpec axisPt = {ptbinning, "#it{p}_{T} GeV/#it{c}"};
    AxisSpec nchAxis = {4000, 0, 4000, "N_{ch}"};
    AxisSpec t0cAxis = {70, 0, 70000, "N_{ch} (T0C)"};
    AxisSpec t0aAxis = {200, 0, 200, "N_{ch}"};
    AxisSpec multpvAxis = {4000, 0, 4000, "N_{ch} (PV)"};
    AxisSpec shclAxis = {200, 0, 1, "Fraction shared cl. TPC"};
    AxisSpec clAxis = {160, 0, 160, "Number of cl. TPC"};

    int ptbins = ptbinning.size() - 1;

    if (cfgFillWeights) {
      fWeights->setPtBins(ptbins, &ptbinning[0]);
      fWeights->init(true, false);

      fWeightsPOS->setPtBins(ptbins, &ptbinning[0]);
      fWeightsPOS->init(true, false);

      fWeightsNEG->setPtBins(ptbins, &ptbinning[0]);
      fWeightsNEG->init(true, false);
    }

    if ((doprocessData || doprocessMCReco)) {
      if (cfgFillQAHistos) {
        registry.add("QA/after/hCent", "", {HistType::kTH1D, {axisCent}});
        registry.add("QA/after/pt_phi", "", {HistType::kTH2D, {axisPt, axisPhiMod}});
        registry.add("QA/after/hPt_inclusive", "", {HistType::kTH1D, {axisPt}});
        registry.add("QA/after/hPt_positive", "", {HistType::kTH1D, {axisPt}});
        registry.add("QA/after/hPt_negative", "", {HistType::kTH1D, {axisPt}});
        registry.add("QA/after/globalTracks_centT0C", "", {HistType::kTH2D, {axisCent, nchAxis}});
        registry.add("QA/after/PVTracks_centT0C", "", {HistType::kTH2D, {axisCent, multpvAxis}});
        registry.add("QA/after/globalTracks_PVTracks", "", {HistType::kTH2D, {multpvAxis, nchAxis}});
        registry.add("QA/after/globalTracks_multT0A", "", {HistType::kTH2D, {t0aAxis, nchAxis}});
        registry.add("QA/after/globalTracks_multV0A", "", {HistType::kTH2D, {t0aAxis, nchAxis}});
        registry.add("QA/after/multV0A_multT0A", "", {HistType::kTH2D, {t0aAxis, t0aAxis}});
        registry.add("QA/after/multT0C_centT0C", "", {HistType::kTH2D, {axisCent, t0cAxis}});
      }

      if (doprocessData) {
        registry.add<TH1>("hSPplaneA", "hSPplaneA", kTH1D, {axisPhiPlane});
        registry.add<TH1>("hSPplaneC", "hSPplaneC", kTH1D, {axisPhiPlane});
        registry.add<TH1>("hSPplaneFull", "hSPplaneFull", kTH1D, {axisPhiPlane});

        registry.add<TProfile>("hCosPhiACosPhiC", "hCosPhiACosPhiC; Centrality(%); #LT Cos(#Psi^{A})Cos(#Psi^{C})#GT", kTProfile, {axisCent});
        registry.add<TProfile>("hSinPhiASinPhiC", "hSinPhiASinPhiC; Centrality(%); #LT Sin(#Psi^{A})Sin(#Psi^{C})#GT", kTProfile, {axisCent});
        registry.add<TProfile>("hSinPhiACosPhiC", "hSinPhiACosPhiC; Centrality(%); #LT Sin(#Psi^{A})Cos(#Psi^{C})#GT", kTProfile, {axisCent});
        registry.add<TProfile>("hCosPhiASinsPhiC", "hCosPhiASinsPhiC; Centrality(%); #LT Cos(#Psi^{A})Sin(#Psi^{C})#GT", kTProfile, {axisCent});
        registry.add<TProfile>("hFullEvPlaneRes", "hFullEvPlaneRes; Centrality(%); -#LT Cos(#Psi^{A} - #Psi^{C})#GT ", kTProfile, {axisCent});

        // track properties per centrality and per eta, pt bin
        registry.add<TProfile>("incl/vnAx_eta", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnAy_eta", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnCx_eta", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnCy_eta", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnC_eta", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnA_eta", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnA_eta_EP", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnC_eta_EP", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnFull_eta_EP", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnAxCxUx_eta_MH", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnAxCyUx_eta_MH", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnAxCyUy_eta_MH", "", kTProfile, {axisEtaVn});
        registry.add<TProfile>("incl/vnAyCxUy_eta_MH", "", kTProfile, {axisEtaVn});

        registry.add<TProfile>("incl/vnAx_pt", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnAy_pt", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnCx_pt", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnCy_pt", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnC_pt", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnA_pt", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnC_pt_odd", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnA_pt_odd", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnCx_pt_odd", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnAx_pt_odd", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnCy_pt_odd", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnAy_pt_odd", "", kTProfile, {axisPt});

        registry.add<TProfile>("incl/vnA_pt_EP", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnC_pt_EP", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnFull_pt_EP", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnAxCxUx_pt_MH", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnAxCyUx_pt_MH", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnAxCyUy_pt_MH", "", kTProfile, {axisPt});
        registry.add<TProfile>("incl/vnAyCxUy_pt_MH", "", kTProfile, {axisPt});

        registry.add<TProfile>("incl/vnC_cent_minEta", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnA_cent_minEta", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnC_cent_plusEta", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnA_cent_plusEta", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnA_cent_EP", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnC_cent_EP", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnFull_cent_EP", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnAxCxUx_cent_MH", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnAxCyUx_cent_MH", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnAxCyUy_cent_MH", "", kTProfile, {axisCent});
        registry.add<TProfile>("incl/vnAyCxUy_cent_MH", "", kTProfile, {axisCent});

        registry.add<TProfile>("qAqCX", "", kTProfile, {axisCent});
        registry.add<TProfile>("qAqCY", "", kTProfile, {axisCent});
        registry.add<TProfile>("qAqCXY", "", kTProfile, {axisCent});
        registry.add<TProfile>("qAXqCY", "", kTProfile, {axisCent});
        registry.add<TProfile>("qAYqCX", "", kTProfile, {axisCent});

        if (cfgFillQAHistos) {
          registry.add("QA/after/PsiA_vs_Cent", "", {HistType::kTH2D, {axisPhiPlane, axisCent}});
          registry.add("QA/after/PsiC_vs_Cent", "", {HistType::kTH2D, {axisPhiPlane, axisCent}});
          registry.add("QA/after/PsiFull_vs_Cent", "", {HistType::kTH2D, {axisPhiPlane, axisCent}});

          registry.add("QA/after/PsiA_vs_Vx", "", {HistType::kTH2D, {axisPhiPlane, axisVx}});
          registry.add("QA/after/PsiC_vs_Vx", "", {HistType::kTH2D, {axisPhiPlane, axisVx}});
          registry.add("QA/after/PsiFull_vs_Vx", "", {HistType::kTH2D, {axisPhiPlane, axisVx}});

          registry.add("QA/after/PsiA_vs_Vy", "", {HistType::kTH2D, {axisPhiPlane, axisVy}});
          registry.add("QA/after/PsiC_vs_Vy", "", {HistType::kTH2D, {axisPhiPlane, axisVy}});
          registry.add("QA/after/PsiFull_vs_Vy", "", {HistType::kTH2D, {axisPhiPlane, axisVy}});

          registry.add("QA/after/PsiA_vs_Vz", "", {HistType::kTH2D, {axisPhiPlane, axisVz}});
          registry.add("QA/after/PsiC_vs_Vz", "", {HistType::kTH2D, {axisPhiPlane, axisVz}});
          registry.add("QA/after/PsiFull_vs_Vz", "", {HistType::kTH2D, {axisPhiPlane, axisVz}});

          registry.add("QA/after/CentFT0C_vs_CentFT0Cvariant1", "", {HistType::kTH2D, {axisCent, axisCent}});
          registry.add("QA/after/CentFT0C_vs_CentFT0M", "", {HistType::kTH2D, {axisCent, axisCent}});
          registry.add("QA/after/CentFT0C_vs_CentFV0A", "", {HistType::kTH2D, {axisCent, axisCent}});
          // registry.add("QA/after/CentFT0C_vs_CentNGlobal", "", {HistType::kTH2D, {axisCent, axisCent}};
        }
      }
      registry.addClone("QA/after/", "QA/before/");
      // track QA for pos, neg, incl
      registry.add<TH1>("incl/QA/hPt", "", kTH1D, {axisPt});
      registry.add<TH1>("incl/QA/hPhi", "", kTH1D, {axisPhi});
      registry.add<TH1>("incl/QA/hPhiCorrected", "", kTH1D, {axisPhi});
      registry.add<TH1>("incl/QA/hEta", "", kTH1D, {axisEta});
      registry.add<TH3>("incl/QA/hPhi_Eta_vz", "", kTH3D, {axisPhi, axisEta, axisVz});
      registry.add<TH2>("incl/QA/hDCAxy_pt", "", kTH2D, {axisPt, axisDCAxy});
      registry.add<TH2>("incl/QA/hDCAz_pt", "", kTH2D, {axisPt, axisDCAz});
      registry.add("incl/QA/hSharedClusters_pt", "", {HistType::kTH2D, {axisPt, shclAxis}});
      registry.add("incl/QA/hCrossedRows_pt", "", {HistType::kTH2D, {axisPt, clAxis}});

      registry.addClone("incl/", "pos/");
      registry.addClone("incl/", "neg/");
    } else if (doprocessMCGen) {
      registry.add("trackMCGen/before/pt_gen_incl", "", {HistType::kTH1D, {axisPt}});
      registry.add("trackMCGen/before/phi_eta_vtxZ_gen", "", {HistType::kTH3D, {axisPhi, axisEta, axisVz}});
      registry.addClone("trackMCGen/before/", "trackMCGen/after/");
    }

    registry.add("hEventCount", "Number of Event; Cut; #Events Passed Cut", {HistType::kTH1D, {{nEventSelections, 0, nEventSelections}}});
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_FilteredEvent + 1, "Filtered event");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_sel8 + 1, "Sel8");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_occupancy + 1, "kOccupancy");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_kTVXinTRD + 1, "kTVXinTRD");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_kNoSameBunchPileup + 1, "kNoSameBunchPileup");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_kIsGoodZvtxFT0vsPV + 1, "kIsGoodZvtxFT0vsPV");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_kNoCollInTimeRangeStandard + 1, "kNoCollInTimeRangeStandard");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_kIsVertexITSTPC + 1, "kIsVertexITSTPC");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_MultCuts + 1, "Mult cuts (Alex)");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_CentCuts + 1, "Cenrality range");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_kIsGoodITSLayersAll + 1, "kkIsGoodITSLayersAll");
    registry.get<TH1>(HIST("hEventCount"))->GetXaxis()->SetBinLabel(evSel_isSelectedZDC + 1, "isSelected");

    registry.add("hTrackCount", "Number of Tracks; Cut; #Tracks Passed Cut", {HistType::kTH1D, {{nTrackSelections, 0, nTrackSelections}}});
    registry.get<TH1>(HIST("hTrackCount"))->GetXaxis()->SetBinLabel(trackSel_FilteredTracks + 1, "Filtered Track");
    registry.get<TH1>(HIST("hTrackCount"))->GetXaxis()->SetBinLabel(trackSel_NCls + 1, "nClusters TPC");
    registry.get<TH1>(HIST("hTrackCount"))->GetXaxis()->SetBinLabel(trackSel_FshCls + 1, "Frac. sh. Cls TPC");
    registry.get<TH1>(HIST("hTrackCount"))->GetXaxis()->SetBinLabel(trackSel_TPCBoundary + 1, "TPC Boundary");
    registry.get<TH1>(HIST("hTrackCount"))->GetXaxis()->SetBinLabel(trackSel_ZeroCharge + 1, "Only charged");
    registry.get<TH1>(HIST("hTrackCount"))->GetXaxis()->SetBinLabel(trackSel_ParticleWeights + 1, "Apply weights");

    if (cfgUseAdditionalEventCut) {
      fMultPVCutLow = new TF1("fMultPVCutLow", "[0]+[1]*x+[2]*x*x+[3]*x*x*x+[4]*x*x*x*x - 3.5*([5]+[6]*x+[7]*x*x+[8]*x*x*x+[9]*x*x*x*x)", 0, 100);
      fMultPVCutLow->SetParameters(3257.29, -121.848, 1.98492, -0.0172128, 6.47528e-05, 154.756, -1.86072, -0.0274713, 0.000633499, -3.37757e-06);
      fMultPVCutHigh = new TF1("fMultPVCutHigh", "[0]+[1]*x+[2]*x*x+[3]*x*x*x+[4]*x*x*x*x + 3.5*([5]+[6]*x+[7]*x*x+[8]*x*x*x+[9]*x*x*x*x)", 0, 100);
      fMultPVCutHigh->SetParameters(3257.29, -121.848, 1.98492, -0.0172128, 6.47528e-05, 154.756, -1.86072, -0.0274713, 0.000633499, -3.37757e-06);

      fMultCutLow = new TF1("fMultCutLow", "[0]+[1]*x+[2]*x*x+[3]*x*x*x - 2.*([4]+[5]*x+[6]*x*x+[7]*x*x*x+[8]*x*x*x*x)", 0, 100);
      fMultCutLow->SetParameters(1654.46, -47.2379, 0.449833, -0.0014125, 150.773, -3.67334, 0.0530503, -0.000614061, 3.15956e-06);
      fMultCutHigh = new TF1("fMultCutHigh", "[0]+[1]*x+[2]*x*x+[3]*x*x*x + 3.*([4]+[5]*x+[6]*x*x+[7]*x*x*x+[8]*x*x*x*x)", 0, 100);
      fMultCutHigh->SetParameters(1654.46, -47.2379, 0.449833, -0.0014125, 150.773, -3.67334, 0.0530503, -0.000614061, 3.15956e-06);
    }

    if (cfgUseAdditionalTrackCut) {
      fPhiCutLow = new TF1("fPhiCutLow", "0.06/x+pi/18.0-0.06", 0, 100);
      fPhiCutHigh = new TF1("fPhiCutHigh", "0.1/x+pi/18.0+0.06", 0, 100);
    }
  }

  int getMagneticField(uint64_t timestamp)
  {
    // TODO done only once (and not per run). Will be replaced by CCDBConfigurable
    // static o2::parameters::GRPObject* grpo = nullptr;
    static o2::parameters::GRPMagField* grpo = nullptr;
    if (grpo == nullptr) {
      // grpo = ccdb->getForTimeStamp<o2::parameters::GRPObject>("GLO/GRP/GRP", timestamp);
      grpo = ccdb->getForTimeStamp<o2::parameters::GRPMagField>("GLO/Config/GRPMagField", timestamp);
      if (grpo == nullptr) {
        LOGF(fatal, "GRP object not found for timestamp %llu", timestamp);
        return 0;
      }
      LOGF(info, "Retrieved GRP for timestamp %llu with magnetic field of %d kG", timestamp, grpo->getNominalL3Field());
    }
    return grpo->getNominalL3Field();
  }

  // From Generic Framework
  void loadCorrections(uint64_t timestamp)
  {
    // corrections saved on CCDB as TList {incl, pos, neg} of GFWWeights (acc) TH1D (eff) objects!
    if (cfg.correctionsLoaded)
      return;

    if (cfgAcceptance.value.empty() == false) {
      TList* listCorrections = ccdb->getForTimeStamp<TList>(cfgAcceptance, timestamp);
      cfg.mAcceptance.push_back(reinterpret_cast<GFWWeights*>(listCorrections->FindObject("weights")));
      cfg.mAcceptance.push_back(reinterpret_cast<GFWWeights*>(listCorrections->FindObject("weights_positive")));
      cfg.mAcceptance.push_back(reinterpret_cast<GFWWeights*>(listCorrections->FindObject("weights_negative")));
      int sizeAcc = cfg.mAcceptance.size();
      if (sizeAcc < 3)
        LOGF(warning, "Could not load acceptance weights from %s", cfgAcceptance.value.c_str());
      else
        LOGF(info, "Loaded acceptance weights from %s", cfgAcceptance.value.c_str());
    } else {
      LOGF(info, "cfgAcceptance empty! No corrections loaded");
    }
    if (cfgEfficiency.value.empty() == false) {
      TList* listCorrections = ccdb->getForTimeStamp<TList>(cfgEfficiency, timestamp);
      cfg.mEfficiency.push_back(reinterpret_cast<TH1D*>(listCorrections->FindObject("Efficiency")));
      cfg.mEfficiency.push_back(reinterpret_cast<TH1D*>(listCorrections->FindObject("Efficiency_pos")));
      cfg.mEfficiency.push_back(reinterpret_cast<TH1D*>(listCorrections->FindObject("Efficiency_neg")));
      int sizeEff = cfg.mEfficiency.size();
      if (sizeEff < 3) {
        LOGF(fatal, "Could not load efficiency histogram for trigger particles from %s", cfgEfficiency.value.c_str());
      }
      LOGF(info, "Loaded efficiency histogram from %s", cfgEfficiency.value.c_str());
    } else {
      LOGF(info, "cfgEfficiency empty! No corrections loaded");
    }
    cfg.correctionsLoaded = true;
  }

  // From Generic Framework
  bool setCurrentParticleWeights(int pID, float& weight_nue, float& weight_nua, const float& phi, const float& eta, const float& pt, const float& vtxz)
  {
    float eff = 1.;
    int sizeEff = cfg.mEfficiency.size();
    if (sizeEff > pID)
      eff = cfg.mEfficiency[pID]->GetBinContent(cfg.mEfficiency[pID]->FindBin(pt));
    else
      eff = 1.0;
    if (eff == 0)
      return false;
    weight_nue = 1. / eff;
    int sizeAcc = cfg.mAcceptance.size();
    if (sizeAcc > pID)
      weight_nua = cfg.mAcceptance[pID]->getNUA(phi, eta, vtxz);
    else
      weight_nua = 1;
    return true;
  }

  template <typename TCollision>
  bool eventSelected(TCollision collision, const int& multTrk, const float& centrality)
  {
    if (!collision.sel8())
      return 0;
    registry.fill(HIST("hEventCount"), evSel_sel8);

    // Occupancy
    if (cfgDoOccupancySel) {
      auto occupancy = collision.trackOccupancyInTimeRange();
      if (occupancy > cfgMaxOccupancy) {
        return 0;
      }
      registry.fill(HIST("hEventCount"), evSel_occupancy);
    }

    if (cfgTVXinTRD) {
      if (collision.alias_bit(kTVXinTRD)) {
        // TRD triggered
        // "CMTVX-B-NOPF-TRD,minbias_TVX"
        return 0;
      }
      registry.fill(HIST("hEventCount"), evSel_kTVXinTRD);
    }

    if (cfgNoSameBunchPileupCut) {
      if (!collision.selection_bit(o2::aod::evsel::kNoSameBunchPileup)) {
        // rejects collisions which are associated with the same "found-by-T0" bunch crossing
        // https://indico.cern.ch/event/1396220/#1-event-selection-with-its-rof
        return 0;
      }
      registry.fill(HIST("hEventCount"), evSel_kNoSameBunchPileup);
    }
    if (cfgIsGoodZvtxFT0vsPV) {
      if (!collision.selection_bit(o2::aod::evsel::kIsGoodZvtxFT0vsPV)) {
        // removes collisions with large differences between z of PV by tracks and z of PV from FT0 A-C time difference
        // use this cut at low multiplicities with caution
        return 0;
      }
      registry.fill(HIST("hEventCount"), evSel_kIsGoodZvtxFT0vsPV);
    }
    if (cfgNoCollInTimeRangeStandard) {
      if (!collision.selection_bit(o2::aod::evsel::kNoCollInTimeRangeStandard)) {
        //  Rejection of the collisions which have other events nearby
        return 0;
      }
      registry.fill(HIST("hEventCount"), evSel_kNoCollInTimeRangeStandard);
    }

    if (cfgIsVertexITSTPC) {
      if (!collision.selection_bit(o2::aod::evsel::kIsVertexITSTPC)) {
        // selects collisions with at least one ITS-TPC track, and thus rejects vertices built from ITS-only tracks
        return 0;
      }
      registry.fill(HIST("hEventCount"), evSel_kIsVertexITSTPC);
    }

    if (cfgUseAdditionalEventCut) {
      float vtxz = -999;
      if (collision.numContrib() > 1) {
        vtxz = collision.posZ();
        float zRes = std::sqrt(collision.covZZ());
        if (zRes > 0.25 && collision.numContrib() < 20)
          vtxz = -999;
      }

      auto multNTracksPV = collision.multNTracksPV();

      if (vtxz > 10 || vtxz < -10)
        return 0;
      if (multNTracksPV < fMultPVCutLow->Eval(centrality))
        return 0;
      if (multNTracksPV > fMultPVCutHigh->Eval(centrality))
        return 0;
      if (multTrk < fMultCutLow->Eval(centrality))
        return 0;
      if (multTrk > fMultCutHigh->Eval(centrality))
        return 0;

      registry.fill(HIST("hEventCount"), evSel_MultCuts);
    }

    if (centrality > cfgCentMax || centrality < cfgCentMin)
      return 0;
    registry.fill(HIST("hEventCount"), evSel_CentCuts);

    if (cfgIsGoodITSLayersAll) {
      if (!collision.selection_bit(o2::aod::evsel::kIsGoodITSLayersAll)) {
        // New event selection bits to cut time intervals with dead ITS staves
        // https://indico.cern.ch/event/1493023/ (09-01-2025)
        return 0;
      }
      registry.fill(HIST("hEventCount"), evSel_kIsGoodITSLayersAll);
    }

    return 1;
  }

  template <typename TrackObject>
  bool trackSelected(TrackObject track, const int& field)
  {

    if (track.tpcNClsFound() < cfgNcls)
      return false;
    registry.fill(HIST("hTrackCount"), trackSel_NCls);

    if (track.tpcFractionSharedCls() > cfgFshcls)
      return false;
    registry.fill(HIST("hTrackCount"), trackSel_FshCls);

    double phimodn = track.phi();
    if (field < 0) // for negative polarity field
      phimodn = o2::constants::math::TwoPI - phimodn;
    if (track.sign() < 0) // for negative charge
      phimodn = o2::constants::math::TwoPI - phimodn;
    if (phimodn < 0)
      LOGF(warning, "phi < 0: %g", phimodn);

    phimodn += o2::constants::math::PI / 18.0; // to center gap in the middle
    phimodn = fmod(phimodn, o2::constants::math::PI / 9.0);
    registry.fill(HIST("QA/before/pt_phi"), track.pt(), phimodn);

    if (cfgUseAdditionalTrackCut) {
      if (phimodn < fPhiCutHigh->Eval(track.pt()) && phimodn > fPhiCutLow->Eval(track.pt()))
        return false; // reject track
    }
    registry.fill(HIST("QA/after/pt_phi"), track.pt(), phimodn);
    registry.fill(HIST("hTrackCount"), trackSel_TPCBoundary);
    return true;
  }

  template <FillType ft, typename CollisionObject, typename TracksObject>
  inline void fillEventQA(CollisionObject collision, TracksObject tracks)
  {
    static constexpr std::string_view Time[] = {"before", "after"};

    registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/hCent"), collision.centFT0C());
    registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/globalTracks_centT0C"), collision.centFT0C(), tracks.size());
    registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PVTracks_centT0C"), collision.centFT0C(), collision.multNTracksPV());
    registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/globalTracks_PVTracks"), collision.multNTracksPV(), tracks.size());
    registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/globalTracks_multT0A"), collision.multFT0A(), tracks.size());
    registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/globalTracks_multV0A"), collision.multFV0A(), tracks.size());
    registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/multV0A_multT0A"), collision.multFT0A(), collision.multFV0A());
    registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/multT0C_centT0C"), collision.centFT0C(), collision.multFT0C());

    if constexpr (framework::has_type_v<aod::sptablezdc::Vx, typename CollisionObject::all_columns>) {
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/CentFT0C_vs_CentFT0Cvariant1"), collision.centFT0C(), collision.centFT0CVariant1());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/CentFT0C_vs_CentFT0M"), collision.centFT0C(), collision.centFT0M());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/CentFT0C_vs_CentFV0A"), collision.centFT0C(), collision.centFV0A());
      // registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/CentFT0C_vs_CentNGlobal"), collision.centFT0C(), collision.centNGlobal());

      double psiA = 1.0 * std::atan2(collision.qyA(), collision.qxA());
      double psiC = 1.0 * std::atan2(collision.qyC(), collision.qxC());
      double psiFull = 1.0 * std::atan2(collision.qyA() + collision.qyC(), collision.qxA() + collision.qxC());

      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiA_vs_Cent"), psiA, collision.centFT0C());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiC_vs_Cent"), psiC, collision.centFT0C());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiFull_vs_Cent"), psiFull, collision.centFT0C());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiA_vs_Vx"), psiA, collision.vx());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiC_vs_Vx"), psiC, collision.vx());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiFull_vs_Vx"), psiFull, collision.vx());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiA_vs_Vy"), psiA, collision.vy());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiC_vs_Vy"), psiC, collision.vy());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiFull_vs_Vy"), psiFull, collision.vy());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiA_vs_Vz"), psiA, collision.posZ());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiC_vs_Vz"), psiC, collision.posZ());
      registry.fill(HIST("QA/") + HIST(Time[ft]) + HIST("/PsiFull_vs_Vz"), psiFull, collision.posZ());
    }
    return;
  }

  template <ChargeType ct, typename TrackObject>
  inline void fillHistograms(TrackObject track, float wacc, float weff, double ux, double uy, double uxMH, double uyMH, double qxA, double qyA, double qxC, double qyC, double corrQQx, double corrQQy, double corrQQ, double vnA, double vnC, double vnFull, double centrality)
  {
    registry.fill(HIST(Charge[ct]) + HIST("vnAx_eta"), track.eta(), (ux * qxA) / std::sqrt(std::fabs(corrQQx)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAy_eta"), track.eta(), (uy * qyA) / std::sqrt(std::fabs(corrQQy)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnCx_eta"), track.eta(), (ux * qxC) / std::sqrt(std::fabs(corrQQx)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnCy_eta"), track.eta(), (uy * qyC) / std::sqrt(std::fabs(corrQQy)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnA_eta"), track.eta(), (uy * qyA + ux * qxA) / std::sqrt(std::fabs(corrQQ)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnC_eta"), track.eta(), (uy * qyC + ux * qxC) / std::sqrt(std::fabs(corrQQ)), wacc * weff);

    registry.fill(HIST(Charge[ct]) + HIST("vnAxCxUx_eta_MH"), track.eta(), (uxMH * qxA * qxC) / corrQQx, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAxCyUx_eta_MH"), track.eta(), (uxMH * qyA * qyC) / corrQQy, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAxCyUy_eta_MH"), track.eta(), (uyMH * qxA * qyC) / corrQQx, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAyCxUy_eta_MH"), track.eta(), (uyMH * qyA * qxC) / corrQQy, wacc * weff);

    registry.fill(HIST(Charge[ct]) + HIST("vnAx_pt"), track.pt(), (ux * qxA) / std::sqrt(std::fabs(corrQQx)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAy_pt"), track.pt(), (uy * qyA) / std::sqrt(std::fabs(corrQQy)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnCx_pt"), track.pt(), (ux * qxC) / std::sqrt(std::fabs(corrQQx)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnCy_pt"), track.pt(), (uy * qyC) / std::sqrt(std::fabs(corrQQy)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnA_pt"), track.pt(), (uy * qyA + ux * qxA) / std::sqrt(std::fabs(corrQQ)), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnC_pt"), track.pt(), (uy * qyC + ux * qxC) / std::sqrt(std::fabs(corrQQ)), wacc * weff);

    registry.fill(HIST(Charge[ct]) + HIST("vnAxCxUx_pt_MH"), track.pt(), (uxMH * qxA * qxC) / corrQQx, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAxCyUx_pt_MH"), track.pt(), (uxMH * qyA * qyC) / corrQQy, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAxCyUy_pt_MH"), track.pt(), (uyMH * qxA * qyC) / corrQQx, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAyCxUy_pt_MH"), track.pt(), (uyMH * qyA * qxC) / corrQQy, wacc * weff);

    registry.fill(HIST(Charge[ct]) + HIST("vnA_eta_EP"), track.eta(), vnA, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnC_eta_EP"), track.eta(), vnC, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnFull_eta_EP"), track.eta(), vnFull, wacc * weff);

    registry.fill(HIST(Charge[ct]) + HIST("vnA_pt_EP"), track.pt(), vnA, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnC_pt_EP"), track.pt(), vnC, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnFull_pt_EP"), track.pt(), vnFull, wacc * weff);

    // For integrated v1 take only tracks from eta>0.
    // Following https://arxiv.org/pdf/1306.4145
    if (track.eta() < 0 && cfgHarm == 1) {
      registry.fill(HIST(Charge[ct]) + HIST("vnA_cent_minEta"), centrality, -1.0 * (uy * qyA + ux * qxA) / std::sqrt(std::fabs(corrQQ)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnC_cent_minEta"), centrality, -1.0 * (uy * qyC + ux * qxC) / std::sqrt(std::fabs(corrQQ)), wacc * weff);

      registry.fill(HIST(Charge[ct]) + HIST("vnA_pt_odd"), track.pt(), -1.0 * (uy * qyA + ux * qxA) / std::sqrt(std::fabs(corrQQ)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnC_pt_odd"), track.pt(), -1.0 * (uy * qyC + ux * qxC) / std::sqrt(std::fabs(corrQQ)), wacc * weff);

      registry.fill(HIST(Charge[ct]) + HIST("vnAx_pt_odd"), track.pt(), -1.0 * (ux * qxA) / std::sqrt(std::fabs(corrQQx)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnAy_pt_odd"), track.pt(), -1.0 * (uy * qyA) / std::sqrt(std::fabs(corrQQy)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnCx_pt_odd"), track.pt(), -1.0 * (ux * qxC) / std::sqrt(std::fabs(corrQQx)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnCy_pt_odd"), track.pt(), -1.0 * (uy * qyC) / std::sqrt(std::fabs(corrQQy)), wacc * weff);

    } else {
      registry.fill(HIST(Charge[ct]) + HIST("vnA_cent_plusEta"), centrality, (uy * qyA + ux * qxA) / std::sqrt(std::fabs(corrQQ)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnC_cent_plusEta"), centrality, (uy * qyC + ux * qxC) / std::sqrt(std::fabs(corrQQ)), wacc * weff);

      registry.fill(HIST(Charge[ct]) + HIST("vnA_pt_odd"), track.pt(), (uy * qyA + ux * qxA) / std::sqrt(std::fabs(corrQQ)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnC_pt_odd"), track.pt(), (uy * qyC + ux * qxC) / std::sqrt(std::fabs(corrQQ)), wacc * weff);

      registry.fill(HIST(Charge[ct]) + HIST("vnAx_pt_odd"), track.pt(), (ux * qxA) / std::sqrt(std::fabs(corrQQx)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnAy_pt_odd"), track.pt(), (uy * qyA) / std::sqrt(std::fabs(corrQQy)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnCx_pt_odd"), track.pt(), (ux * qxC) / std::sqrt(std::fabs(corrQQx)), wacc * weff);
      registry.fill(HIST(Charge[ct]) + HIST("vnCy_pt_odd"), track.pt(), (uy * qyC) / std::sqrt(std::fabs(corrQQy)), wacc * weff);
    }

    registry.fill(HIST(Charge[ct]) + HIST("vnAxCxUx_cent_MH"), centrality, (uxMH * qxA * qxC) / corrQQx, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAxCyUx_cent_MH"), centrality, (uxMH * qyA * qyC) / corrQQy, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAxCyUy_cent_MH"), centrality, (uyMH * qxA * qyC) / corrQQx, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnAyCxUy_cent_MH"), centrality, (uyMH * qyA * qxC) / corrQQy, wacc * weff);

    registry.fill(HIST(Charge[ct]) + HIST("vnA_cent_EP"), centrality, vnA, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnC_cent_EP"), centrality, vnC, wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("vnFull_cent_EP"), centrality, vnFull, wacc * weff);
  }

  template <ChargeType ct, typename TrackObject>
  inline void fillTrackQA(TrackObject track, double vz, float wacc = 1, float weff = 1)
  {
    registry.fill(HIST(Charge[ct]) + HIST("QA/hPt"), track.pt());
    registry.fill(HIST(Charge[ct]) + HIST("QA/hPhi"), track.phi());
    registry.fill(HIST(Charge[ct]) + HIST("QA/hPhiCorrected"), track.phi(), wacc * weff);
    registry.fill(HIST(Charge[ct]) + HIST("QA/hEta"), track.eta());
    registry.fill(HIST(Charge[ct]) + HIST("QA/hPhi_Eta_vz"), track.phi(), track.eta(), vz);
    registry.fill(HIST(Charge[ct]) + HIST("QA/hDCAxy_pt"), track.pt(), track.dcaXY());
    registry.fill(HIST(Charge[ct]) + HIST("QA/hDCAz_pt"), track.pt(), track.dcaZ());
    registry.fill(HIST(Charge[ct]) + HIST("QA/hSharedClusters_pt"), track.pt(), track.tpcFractionSharedCls());
    registry.fill(HIST(Charge[ct]) + HIST("QA/hCrossedRows_pt"), track.pt(), track.tpcNClsFound());
  }

  void processData(UsedCollisions::iterator const& collision, aod::BCsWithTimestamps const&, UsedTracks const& tracks)
  {
    registry.fill(HIST("hEventCount"), evSel_FilteredEvent);

    auto bc = collision.bc_as<aod::BCsWithTimestamps>();
    auto field = (cfgMagField == 99999) ? getMagneticField(bc.timestamp()) : cfgMagField;

    if (bc.runNumber() != cfg.lastRunNumber) {
      // load corrections again for new run!
      cfg.correctionsLoaded = false;
      cfg.lastRunNumber = bc.runNumber();
    }
    if (cfgFillQAHistos)
      fillEventQA<kBefore>(collision, tracks);

    loadCorrections(bc.timestamp());

    float centrality = collision.centFT0C();

    if (cfgFT0Cvariant1)
      centrality = collision.centFT0CVariant1();
    if (cfgFT0M)
      centrality = collision.centFT0M();
    if (cfgFV0A)
      centrality = collision.centFV0A();
    if (cfgNGlobal)
      centrality = collision.centNGlobal();

    if (!eventSelected(collision, tracks.size(), centrality))
      return;

    if (collision.isSelected()) {

      registry.fill(HIST("hEventCount"), evSel_isSelectedZDC);

      double qxA = collision.qxA();
      double qyA = collision.qyA();
      double qxC = collision.qxC();
      double qyC = collision.qyC();

      double vtxz = collision.posZ();

      double psiA = 1.0 * std::atan2(qyA, qxA);
      registry.fill(HIST("hSPplaneA"), psiA, 1);

      double psiC = 1.0 * std::atan2(qyC, qxC);
      registry.fill(HIST("hSPplaneC"), psiC, 1);

      // https://twiki.cern.ch/twiki/pub/ALICE/DirectedFlowAnalysisNote/vn_ZDC_ALICE_INT_NOTE_version02.pdf
      double psiFull = 1.0 * std::atan2(qyA + qyC, qxA + qxC);
      registry.fill(HIST("hSPplaneFull"), psiFull, 1);

      if (cfgFillQAHistos)
        fillEventQA<kAfter>(collision, tracks);

      registry.fill(HIST("hCosPhiACosPhiC"), centrality, std::cos(psiA) * std::cos(psiC));
      registry.fill(HIST("hSinPhiASinPhiC"), centrality, std::sin(psiA) * std::sin(psiC));
      registry.fill(HIST("hSinPhiACosPhiC"), centrality, std::sin(psiA) * std::cos(psiC));
      registry.fill(HIST("hCosPhiASinsPhiC"), centrality, std::cos(psiA) * std::sin(psiC));

      registry.fill(HIST("hFullEvPlaneRes"), centrality, -1 * std::cos(psiA - psiC));

      registry.fill(HIST("qAqCXY"), centrality, qxA * qxC + qyA * qyC);

      registry.fill(HIST("qAXqCY"), centrality, qxA * qyC);
      registry.fill(HIST("qAYqCX"), centrality, qyA * qxC);

      registry.fill(HIST("qAqCX"), centrality, qxA * qxC);
      registry.fill(HIST("qAqCY"), centrality, qyA * qyC);

      double corrQQ = 1., corrQQx = 1., corrQQy = 1.;

      // Load correlations and SP resolution needed for Scalar Product and event plane methods.
      // Only load once!
      // If not loaded set to 1
      if (cfgLoadAverageQQ) {
        if (!cfg.clQQ) {
          TList* hcorrList = ccdb->getForTimeStamp<TList>(cfgCCDBdir.value, bc.timestamp());
          cfg.hcorrQQ = reinterpret_cast<TProfile*>(hcorrList->FindObject("qAqCXY"));
          cfg.hcorrQQx = reinterpret_cast<TProfile*>(hcorrList->FindObject("qAqCX"));
          cfg.hcorrQQy = reinterpret_cast<TProfile*>(hcorrList->FindObject("qAqCY"));
          cfg.clQQ = true;
        }
        corrQQ = cfg.hcorrQQ->GetBinContent(cfg.hcorrQQ->FindBin(centrality));
        corrQQx = cfg.hcorrQQx->GetBinContent(cfg.hcorrQQx->FindBin(centrality));
        corrQQy = cfg.hcorrQQy->GetBinContent(cfg.hcorrQQy->FindBin(centrality));
      }

      double evPlaneRes = 1.;
      if (cfgLoadSPPlaneRes) {
        if (!cfg.clEvPlaneRes) {
          cfg.hEvPlaneRes = ccdb->getForTimeStamp<TProfile>(cfgCCDBdir_SP.value, bc.timestamp());
          cfg.clEvPlaneRes = true;
        }
        evPlaneRes = cfg.hEvPlaneRes->GetBinContent(cfg.hEvPlaneRes->FindBin(centrality));
        if (evPlaneRes < 0)
          LOGF(fatal, "<Cos(PsiA-PsiC)> > 0 for centrality %.2f! Cannot determine resolution.. Change centrality ranges!!!", centrality);
        evPlaneRes = std::sqrt(evPlaneRes);
      }

      for (const auto& track : tracks) {
        registry.fill(HIST("QA/before/hPt_inclusive"), track.pt());
        registry.fill(HIST("hTrackCount"), trackSel_FilteredTracks);

        float weff = 1, wacc = 1;
        float weffP = 1, waccP = 1;
        float weffN = 1, waccN = 1;

        if (!trackSelected(track, field))
          return;

        if (track.sign() == 0.0)
          return;
        registry.fill(HIST("hTrackCount"), trackSel_ZeroCharge);
        bool pos = (track.sign() > 0) ? true : false;

        // Fill NUA weights
        if (cfgFillWeights) {
          fWeights->fill(track.phi(), track.eta(), vtxz, track.pt(), centrality, 0);
        } else if (cfgFillWeightsPOS) {
          if (pos)
            fWeightsPOS->fill(track.phi(), track.eta(), vtxz, track.pt(), centrality, 0);
        } else if (cfgFillWeightsNEG) {
          if (!pos)
            fWeightsNEG->fill(track.phi(), track.eta(), vtxz, track.pt(), centrality, 0);
        }

        // Set weff and wacc for inclusice, negative and positive hadrons
        if (!setCurrentParticleWeights(kInclusive, weff, wacc, track.phi(), track.eta(), track.pt(), vtxz))
          return;
        if (pos && !setCurrentParticleWeights(kPositive, weffP, waccP, track.phi(), track.eta(), track.pt(), vtxz))
          return;
        if (!pos && !setCurrentParticleWeights(kNegative, weffN, waccN, track.phi(), track.eta(), track.pt(), vtxz))
          return;

        registry.fill(HIST("hTrackCount"), trackSel_ParticleWeights);

        registry.fill(HIST("QA/after/hPt_inclusive"), track.pt(), wacc * weff);

        // // constrain angle to 0 -> [0,0+2pi]
        auto phi = RecoDecay::constrainAngle(track.phi(), 0);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        auto ux = std::cos(cfgHarm * phi);
        auto uy = std::sin(cfgHarm * phi);

        auto uxMH = std::cos(cfgHarmMixed * phi);
        auto uyMH = std::sin(cfgHarmMixed * phi);
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

        double vnA = std::cos(cfgHarm * (phi - psiA)) / evPlaneRes;
        double vnC = std::cos(cfgHarm * (phi - psiC)) / evPlaneRes;
        double vnFull = std::cos(cfgHarm * (phi - psiFull)) / evPlaneRes;

        fillHistograms<kInclusive>(track, wacc, weff, ux, uy, uxMH, uyMH, qxA, qyA, qxC, qyC, corrQQx, corrQQy, corrQQ, vnA, vnC, vnFull, centrality);
        fillTrackQA<kInclusive>(track, vtxz, wacc, weff);
        if (pos) {
          fillHistograms<kPositive>(track, waccP, weffP, ux, uy, uxMH, uyMH, qxA, qyA, qxC, qyC, corrQQx, corrQQy, corrQQ, vnA, vnC, vnFull, centrality);
          fillTrackQA<kPositive>(track, vtxz, wacc, weff);
        } else {
          fillHistograms<kNegative>(track, waccN, weffN, ux, uy, uxMH, uyMH, qxA, qyA, qxC, qyC, corrQQx, corrQQy, corrQQ, vnA, vnC, vnFull, centrality);
          fillTrackQA<kNegative>(track, vtxz, wacc, weff);
        }
      } // end of track loop
    } // end of collision isSelected loop
  }
  PROCESS_SWITCH(FlowSP, processData, "Process analysis for non-derived data", true);

  void processMCReco(soa::Filtered<soa::Join<aod::Collisions, aod::EvSels, aod::Mults, aod::CentFT0Cs, aod::CentFT0CVariant1s, aod::CentFT0Ms, aod::CentFV0As, aod::CentNGlobals>>::iterator const& collision, aod::BCsWithTimestamps const&, soa::Filtered<soa::Join<aod::Tracks, aod::TracksExtra, aod::TrackSelection, aod::TracksDCA, aod::McTrackLabels>> const& tracks, aod::McParticles const&)
  {
    auto bc = collision.template bc_as<aod::BCsWithTimestamps>();
    auto field = (cfgMagField == 99999) ? getMagneticField(bc.timestamp()) : cfgMagField;

    double vtxz = collision.posZ();
    float centrality = collision.centFT0C();
    if (cfgFT0Cvariant1)
      centrality = collision.centFT0CVariant1();
    if (cfgFT0M)
      centrality = collision.centFT0M();
    if (cfgFV0A)
      centrality = collision.centFV0A();
    if (cfgNGlobal)
      centrality = collision.centNGlobal();

    if (cfgFillQAHistos)
      fillEventQA<kBefore>(collision, tracks);

    if (!eventSelected(collision, tracks.size(), centrality))
      return;

    if (cfgFillQAHistos)
      fillEventQA<kAfter>(collision, tracks);

    for (const auto& track : tracks) {

      auto mcParticle = track.mcParticle();
      if (!mcParticle.isPhysicalPrimary())
        return;

      if (mcParticle.eta() < -cfgEta || mcParticle.eta() > cfgEta || mcParticle.pt() < cfgPtmin || mcParticle.pt() > cfgPtmax || track.tpcNClsFound() < cfgNcls)
        return;

      if (track.sign() == 0.0)
        return;
      bool pos = (track.sign() > 0) ? true : false;

      registry.fill(HIST("QA/before/hPt_inclusive"), track.pt());
      if (pos) {
        registry.fill(HIST("QA/before/hPt_positive"), track.pt());
      } else {
        registry.fill(HIST("QA/before/hPt_negative"), track.pt());
      }

      if (!trackSelected(track, field))
        return;

      registry.fill(HIST("QA/after/hPt_inclusive"), track.pt());

      fillTrackQA<kInclusive>(track, vtxz);

    } // end of track loop
  }
  PROCESS_SWITCH(FlowSP, processMCReco, "Process analysis for MC reconstructed events", false);

  Filter mcCollFilter = nabs(aod::mccollision::posZ) < cfgVtxZ;
  void processMCGen(soa::Filtered<aod::McCollisions>::iterator const& mcCollision, soa::SmallGroups<soa::Join<aod::McCollisionLabels, aod::Collisions, aod::CentFT0Cs, aod::CentFV0As, aod::CentFT0CVariant1s, aod::CentFT0Ms, aod::CentNGlobals>> const& collisions, aod::McParticles const& particles)
  {
    if (collisions.size() != 1)
      return;
    float centrality = -1;
    for (const auto& collision : collisions) {
      centrality = collision.centFT0C();
      if (cfgFT0Cvariant1)
        centrality = collision.centFT0CVariant1();
      if (cfgFT0M)
        centrality = collision.centFT0M();
      if (cfgFV0A)
        centrality = collision.centFV0A();
      if (cfgNGlobal)
        centrality = collision.centNGlobal();
    }

    if (particles.size() < 1)
      return;
    if (centrality < cfgCentMin || centrality > cfgCentMax)
      return;

    float vtxz = mcCollision.posZ();

    for (const auto& track : particles) {

      if (!track.isPhysicalPrimary())
        continue;

      registry.fill(HIST("trackMCGen/before/pt_gen_incl"), track.pt());
      registry.fill(HIST("trackMCGen/before/phi_eta_vtxZ_gen"), track.phi(), track.eta(), vtxz);

      if (track.eta() < -cfgEta || track.eta() > cfgEta || track.pt() < cfgPtmin || track.pt() > cfgPtmax)
        return;

      registry.fill(HIST("trackMCGen/after/pt_gen_incl"), track.pt());
      registry.fill(HIST("trackMCGen/after/phi_eta_vtxZ_gen"), track.phi(), track.eta(), vtxz);
    }
  }
  PROCESS_SWITCH(FlowSP, processMCGen, "Process analysis for MC generated events", false);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{
    adaptAnalysisTask<FlowSP>(cfgc)};
}
