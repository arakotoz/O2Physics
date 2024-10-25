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
//
// This is a task that employs the standard V0 tables and attempts to combine
// two V0s into a Sigma0 -> Lambda + gamma candidate.
//  *+-+*+-+*+-+*+-+*+-+*+-+*+-+*+-+*
//  Sigma0 builder task
//  *+-+*+-+*+-+*+-+*+-+*+-+*+-+*+-+*
//
//    Comments, questions, complaints, suggestions?
//    Please write to:
//    gianni.shigeru.setoue.liveraro@cern.ch
//

#include <Math/Vector4D.h>
#include <cmath>
#include <array>
#include <cstdlib>

#include "Framework/runDataProcessing.h"
#include "Framework/AnalysisTask.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/ASoAHelpers.h"
#include "Framework/ASoA.h"
#include "ReconstructionDataFormats/Track.h"
#include "Common/Core/RecoDecay.h"
#include "Common/Core/trackUtilities.h"
#include "PWGLF/DataModel/LFStrangenessTables.h"
#include "PWGLF/DataModel/LFStrangenessPIDTables.h"
#include "PWGLF/DataModel/LFStrangenessMLTables.h"
#include "PWGLF/DataModel/LFSigmaTables.h"
#include "Common/Core/TrackSelection.h"
#include "Common/DataModel/TrackSelectionTables.h"
#include "Common/DataModel/EventSelection.h"
#include "Common/DataModel/Centrality.h"
#include "Common/DataModel/PIDResponse.h"
#include "CCDB/BasicCCDBManager.h"
#include <TFile.h>
#include <TH2F.h>
#include <TProfile.h>
#include <TLorentzVector.h>
#include <TPDGCode.h>
#include <TDatabasePDG.h>

using namespace o2;
using namespace o2::framework;
using namespace o2::framework::expressions;
using std::array;
using std::cout;
using std::endl;
using dauTracks = soa::Join<aod::DauTrackExtras, aod::DauTrackTPCPIDs>;
using V0DerivedMCDatas = soa::Join<aod::V0Cores, aod::V0CollRefs, aod::V0Extras, aod::V0MCDatas>;
using V0MLDerivedDatas = soa::Join<aod::V0Cores, aod::V0CollRefs, aod::V0Extras, aod::V0LambdaMLScores, aod::V0GammaMLScores, aod::V0AntiLambdaMLScores>;
using V0StandardDerivedDatas = soa::Join<aod::V0Cores, aod::V0CollRefs, aod::V0Extras>;

struct sigma0builder {
  SliceCache cache;

  Produces<aod::Sigma0Collision> sigma0Coll;          // characterises collisions
  Produces<aod::Sigma0CollRefs> sigma0CollRefs;       // characterises collisions
  Produces<aod::Sigma0Cores> sigma0cores;             // save sigma0 candidates for analysis
  Produces<aod::SigmaPhotonExtras> sigmaPhotonExtras; // save sigma0 candidates for analysis
  Produces<aod::SigmaLambdaExtras> sigmaLambdaExtras; // save sigma0 candidates for analysis
  Produces<aod::SigmaMCCores> sigma0mccores;

  // For manual sliceBy
  Preslice<V0DerivedMCDatas> perCollisionMCDerived = o2::aod::v0data::straCollisionId;
  Preslice<V0StandardDerivedDatas> perCollisionSTDDerived = o2::aod::v0data::straCollisionId;
  Preslice<V0MLDerivedDatas> perCollisionMLDerived = o2::aod::v0data::straCollisionId;

  // Histogram registry
  HistogramRegistry histos{"Histos", {}, OutputObjHandlingPolicy::AnalysisObject};

  // For ML Selection
  Configurable<float> Gamma_MLThreshold{"Gamma_MLThreshold", 0.1, "Decision Threshold value to select gammas"};
  Configurable<float> Lambda_MLThreshold{"Lambda_MLThreshold", 0.1, "Decision Threshold value to select lambdas"};
  Configurable<float> AntiLambda_MLThreshold{"AntiLambda_MLThreshold", 0.1, "Decision Threshold value to select antilambdas"};

  // For standard approach:
  //// Lambda criteria:
  Configurable<float> LambdaDauPseudoRap{"LambdaDauPseudoRap", 1.0, "Max pseudorapidity of daughter tracks"};
  Configurable<float> LambdaMinDCANegToPv{"LambdaMinDCANegToPv", .01, "min DCA Neg To PV (cm)"};
  Configurable<float> LambdaMinDCAPosToPv{"LambdaMinDCAPosToPv", .01, "min DCA Pos To PV (cm)"};
  Configurable<float> LambdaMaxDCAV0Dau{"LambdaMaxDCAV0Dau", 3.5, "Max DCA V0 Daughters (cm)"};
  Configurable<float> LambdaMinv0radius{"LambdaMinv0radius", 0.1, "Min V0 radius (cm)"};
  Configurable<float> LambdaMaxv0radius{"LambdaMaxv0radius", 200, "Max V0 radius (cm)"};
  Configurable<float> LambdaWindow{"LambdaWindow", 0.01, "Mass window around expected (in GeV/c2)"};

  //// Photon criteria:
  Configurable<float> PhotonMaxDauPseudoRap{"PhotonMaxDauPseudoRap", 1.0, "Max pseudorapidity of daughter tracks"};
  Configurable<float> PhotonMinDCAToPv{"PhotonMinDCAToPv", 0.001, "Min DCA daughter To PV (cm)"};
  Configurable<float> PhotonMaxDCAV0Dau{"PhotonMaxDCAV0Dau", 3.0, "Max DCA V0 Daughters (cm)"};
  Configurable<float> PhotonMinRadius{"PhotonMinRadius", 0.5, "Min photon conversion radius (cm)"};
  Configurable<float> PhotonMaxRadius{"PhotonMaxRadius", 250, "Max photon conversion radius (cm)"};
  Configurable<float> PhotonMaxMass{"PhotonMaxMass", 0.3, "Max photon mass (GeV/c^{2})"};

  //// Sigma0 criteria:
  Configurable<float> Sigma0Window{"Sigma0Window", 0.05, "Mass window around expected (in GeV/c2)"};
  Configurable<float> SigmaMaxRap{"SigmaMaxRap", 0.5, "Max sigma0 rapidity"};

  // Axis
  // base properties
  ConfigurableAxis vertexZ{"vertexZ", {30, -15.0f, 15.0f}, ""};
  ConfigurableAxis axisPt{"axisPt", {VARIABLE_WIDTH, 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f, 2.2f, 2.4f, 2.6f, 2.8f, 3.0f, 3.2f, 3.4f, 3.6f, 3.8f, 4.0f, 4.4f, 4.8f, 5.2f, 5.6f, 6.0f, 6.5f, 7.0f, 7.5f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 17.0f, 19.0f, 21.0f, 23.0f, 25.0f, 30.0f, 35.0f, 40.0f, 50.0f}, "pt axis for analysis"};
  ConfigurableAxis axisCentrality{"axisCentrality", {VARIABLE_WIDTH, 0.0f, 5.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f, 110.0f}, "Centrality"};
  ConfigurableAxis axisSigmaMass{"axisSigmaMass", {200, 1.16f, 1.23f}, "M_{#Sigma^{0}} (GeV/c^{2})"};
  ConfigurableAxis axisDeltaPt{"axisDeltaPt", {100, -1.0, +1.0}, "#Delta(p_{T})"};

  int nSigmaCandidates = 0;
  void init(InitContext const&)
  {
    // Event counter
    histos.add("hEventVertexZ", "hEventVertexZ", kTH1F, {vertexZ});
    histos.add("hEventCentrality", "hEventCentrality", kTH1F, {{20, -100.0f, 100.0f}});
    histos.add("hCandidateBuilderSelection", "hCandidateBuilderSelection", kTH1F, {{11, -0.5f, +11.5f}});
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(1, "Photon Mass Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(2, "Photon DauEta Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(3, "Photon DCAToPV Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(4, "Photon DCADau Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(5, "Photon Radius Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(6, "Lambda Mass Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(7, "Lambda DauEta Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(8, "Lambda DCAToPV Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(9, "Lambda Radius Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(10, "Lambda DCADau Cut");
    histos.get<TH1>(HIST("hCandidateBuilderSelection"))->GetXaxis()->SetBinLabel(11, "Sigma Window");

    // For efficiency calculation (and QA):
    histos.add("Efficiency/h2dPtVsCentrality_GammaAll", "h2dPtVsCentrality_GammaAll", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_LambdaAll", "h2dPtVsCentrality_LambdaAll", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_AntiLambdaAll", "h2dPtVsCentrality_AntiLambdaAll", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_GammaSigma0", "h2dPtVsCentrality_GammaSigma0", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_LambdaSigma0", "h2dPtVsCentrality_LambdaSigma0", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_Sigma0All", "h2dPtVsCentrality_Sigma0All", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_Sigma0AfterSel", "h2dPtVsCentrality_Sigma0AfterSel", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_AntiSigma0All", "h2dPtVsCentrality_AntiSigma0All", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_GammaAntiSigma0", "h2dPtVsCentrality_GammaAntiSigma0", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_LambdaAntiSigma0", "h2dPtVsCentrality_LambdaAntiSigma0", kTH2D, {axisCentrality, axisPt});
    histos.add("Efficiency/h2dPtVsCentrality_AntiSigma0AfterSel", "h2dPtVsCentrality_AntiSigma0AfterSel", kTH2D, {axisCentrality, axisPt});

    histos.add("Efficiency/h2dSigmaPtVsLambdaPt", "h2dSigmaPtVsLambdaPt", kTH2D, {axisPt, axisPt});
    histos.add("Efficiency/h2dSigmaPtVsGammaPt", "h2dSigmaPtVsGammaPt", kTH2D, {axisPt, axisPt});

    histos.add("Efficiency/h2dLambdaPtResolution", "h2dLambdaPtResolution", kTH2D, {axisPt, axisDeltaPt});
    histos.add("Efficiency/h2dGammaPtResolution", "h2dGammaPtResolution", kTH2D, {axisPt, axisDeltaPt});

    histos.add("h3dMassSigmasAll", "h3dMassSigmasAll", kTH3F, {axisCentrality, axisPt, axisSigmaMass});
    histos.add("h3dMassSigmasAfterSel", "h3dMassSigmasAfterSel", kTH3F, {axisCentrality, axisPt, axisSigmaMass});
  }

  // Process sigma candidate and store properties in object
  template <typename TV0Object>
  bool processSigmaCandidate(TV0Object const& lambda, TV0Object const& gamma)
  {
    if ((lambda.v0Type() == 0) || (gamma.v0Type() == 0))
      return false;

    if constexpr (
      requires { gamma.gammaBDTScore(); } &&
      requires { lambda.lambdaBDTScore(); } &&
      requires { lambda.antiLambdaBDTScore(); }) {

      LOGF(info, "X-check: ML Selection is on!");
      // Gamma selection:
      if (gamma.gammaBDTScore() <= Gamma_MLThreshold)
        return false;

      // Lambda and AntiLambda selection
      if ((lambda.lambdaBDTScore() <= Lambda_MLThreshold) && (lambda.antiLambdaBDTScore() <= AntiLambda_MLThreshold))
        return false;

    } else {
      // Standard selection
      // Gamma basic selection criteria:
      if (TMath::Abs(gamma.mGamma()) > PhotonMaxMass)
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 0.);
      if ((TMath::Abs(gamma.negativeeta()) > PhotonMaxDauPseudoRap) || (TMath::Abs(gamma.positiveeta()) > PhotonMaxDauPseudoRap))
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 1.);
      if ((TMath::Abs(gamma.dcapostopv()) < PhotonMinDCAToPv) || (TMath::Abs(gamma.dcanegtopv()) < PhotonMinDCAToPv))
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 2.);
      if (TMath::Abs(gamma.dcaV0daughters()) > PhotonMaxDCAV0Dau)
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 3.);
      if ((gamma.v0radius() < PhotonMinRadius) || (gamma.v0radius() > PhotonMaxRadius))
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 4.);

      // Lambda basic selection criteria:
      if ((TMath::Abs(lambda.mLambda() - 1.115683) > LambdaWindow) && (TMath::Abs(lambda.mAntiLambda() - 1.115683) > LambdaWindow))
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 5.);
      if ((TMath::Abs(lambda.negativeeta()) > LambdaDauPseudoRap) || (TMath::Abs(lambda.positiveeta()) > LambdaDauPseudoRap))
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 6.);
      if ((TMath::Abs(lambda.dcapostopv()) < LambdaMinDCAPosToPv) || (TMath::Abs(lambda.dcanegtopv()) < LambdaMinDCANegToPv))
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 7.);
      if ((lambda.v0radius() < LambdaMinv0radius) || (lambda.v0radius() > LambdaMaxv0radius))
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 8.);
      if (TMath::Abs(lambda.dcaV0daughters()) > LambdaMaxDCAV0Dau)
        return false;
      histos.fill(HIST("hCandidateBuilderSelection"), 9.);
    }
    // Sigma0 candidate properties
    std::array<float, 3> pVecPhotons{gamma.px(), gamma.py(), gamma.pz()};
    std::array<float, 3> pVecLambda{lambda.px(), lambda.py(), lambda.pz()};
    auto arrMom = std::array{pVecPhotons, pVecLambda};
    float sigmamass = RecoDecay::m(arrMom, std::array{o2::constants::physics::MassPhoton, o2::constants::physics::MassLambda0});
    float sigmarap = RecoDecay::y(std::array{gamma.px() + lambda.px(), gamma.py() + lambda.py(), gamma.pz() + lambda.pz()}, o2::constants::physics::MassSigma0);

    if (TMath::Abs(sigmamass - 1.192642) > Sigma0Window)
      return false;

    if (TMath::Abs(sigmarap) > SigmaMaxRap)
      return false;

    histos.fill(HIST("hCandidateBuilderSelection"), 10.);

    return true;
  }
  // Helper struct to pass v0 information
  struct {
    float mass;
    float pT;
    float Rapidity;
  } sigmaCandidate;

  // Fill tables with reconstructed sigma0 candidate
  template <typename TV0Object>
  void fillTables(TV0Object const& lambda, TV0Object const& gamma)
  {

    float GammaBDTScore = -1;
    float LambdaBDTScore = -1;
    float AntiLambdaBDTScore = -1;

    if constexpr (
      requires { gamma.gammaBDTScore(); } &&
      requires { lambda.lambdaBDTScore(); } &&
      requires { lambda.antiLambdaBDTScore(); }) {

      GammaBDTScore = gamma.gammaBDTScore();
      LambdaBDTScore = lambda.lambdaBDTScore();
      AntiLambdaBDTScore = lambda.antiLambdaBDTScore();
    }

    // Sigma0 candidate properties
    std::array<float, 3> pVecPhotons{gamma.px(), gamma.py(), gamma.pz()};
    std::array<float, 3> pVecLambda{lambda.px(), lambda.py(), lambda.pz()};
    auto arrMom = std::array{pVecPhotons, pVecLambda};
    sigmaCandidate.mass = RecoDecay::m(arrMom, std::array{o2::constants::physics::MassPhoton, o2::constants::physics::MassLambda0});
    sigmaCandidate.pT = RecoDecay::pt(array{gamma.px() + lambda.px(), gamma.py() + lambda.py()});
    sigmaCandidate.Rapidity = RecoDecay::y(std::array{gamma.px() + lambda.px(), gamma.py() + lambda.py(), gamma.pz() + lambda.pz()}, o2::constants::physics::MassSigma0);

    // Sigma related
    float fSigmapT = sigmaCandidate.pT;
    float fSigmaMass = sigmaCandidate.mass;
    float fSigmaRap = sigmaCandidate.Rapidity;

    // Daughters related
    /// Photon
    auto posTrackGamma = gamma.template posTrackExtra_as<dauTracks>();
    auto negTrackGamma = gamma.template negTrackExtra_as<dauTracks>();

    float fPhotonPt = gamma.pt();
    float fPhotonMass = gamma.mGamma();
    float fPhotonQt = gamma.qtarm();
    float fPhotonAlpha = gamma.alpha();
    float fPhotonRadius = gamma.v0radius();
    float fPhotonCosPA = gamma.v0cosPA();
    float fPhotonDCADau = gamma.dcaV0daughters();
    float fPhotonDCANegPV = gamma.dcanegtopv();
    float fPhotonDCAPosPV = gamma.dcapostopv();
    float fPhotonZconv = gamma.z();
    float fPhotonEta = gamma.eta();
    float fPhotonY = RecoDecay::y(std::array{gamma.px(), gamma.py(), gamma.pz()}, o2::constants::physics::MassGamma);
    float fPhotonPosTPCNSigma = posTrackGamma.tpcNSigmaEl();
    float fPhotonNegTPCNSigma = negTrackGamma.tpcNSigmaEl();
    uint8_t fPhotonPosTPCCrossedRows = posTrackGamma.tpcCrossedRows();
    uint8_t fPhotonNegTPCCrossedRows = negTrackGamma.tpcCrossedRows();
    float fPhotonPosPt = gamma.positivept();
    float fPhotonNegPt = gamma.negativept();
    float fPhotonPosEta = gamma.positiveeta();
    float fPhotonNegEta = gamma.negativeeta();
    float fPhotonPosY = RecoDecay::y(std::array{gamma.pxpos(), gamma.pypos(), gamma.pzpos()}, o2::constants::physics::MassElectron);
    float fPhotonNegY = RecoDecay::y(std::array{gamma.pxneg(), gamma.pyneg(), gamma.pzneg()}, o2::constants::physics::MassElectron);
    float fPhotonPsiPair = gamma.psipair();
    int fPhotonPosITSCls = posTrackGamma.itsNCls();
    int fPhotonNegITSCls = negTrackGamma.itsNCls();
    uint32_t fPhotonPosITSClSize = posTrackGamma.itsClusterSizes();
    uint32_t fPhotonNegITSClSize = negTrackGamma.itsClusterSizes();
    uint8_t fPhotonV0Type = gamma.v0Type();

    // Lambda
    auto posTrackLambda = lambda.template posTrackExtra_as<dauTracks>();
    auto negTrackLambda = lambda.template negTrackExtra_as<dauTracks>();

    float fLambdaPt = lambda.pt();
    float fLambdaMass = lambda.mLambda();
    float fAntiLambdaMass = lambda.mAntiLambda();
    float fLambdaQt = lambda.qtarm();
    float fLambdaAlpha = lambda.alpha();
    float fLambdaRadius = lambda.v0radius();
    float fLambdaCosPA = lambda.v0cosPA();
    float fLambdaDCADau = lambda.dcaV0daughters();
    float fLambdaDCANegPV = lambda.dcanegtopv();
    float fLambdaDCAPosPV = lambda.dcapostopv();
    float fLambdaEta = lambda.eta();
    float fLambdaY = lambda.yLambda();
    float fLambdaPosPrTPCNSigma = posTrackLambda.tpcNSigmaPr();
    float fLambdaPosPiTPCNSigma = posTrackLambda.tpcNSigmaPi();
    float fLambdaNegPrTPCNSigma = negTrackLambda.tpcNSigmaPr();
    float fLambdaNegPiTPCNSigma = negTrackLambda.tpcNSigmaPi();
    uint8_t fLambdaPosTPCCrossedRows = posTrackLambda.tpcCrossedRows();
    uint8_t fLambdaNegTPCCrossedRows = negTrackLambda.tpcCrossedRows();
    float fLambdaPosPt = lambda.positivept();
    float fLambdaNegPt = lambda.negativept();
    float fLambdaPosEta = lambda.positiveeta();
    float fLambdaNegEta = lambda.negativeeta();
    float fLambdaPosPrY = RecoDecay::y(std::array{lambda.pxpos(), lambda.pypos(), lambda.pzpos()}, o2::constants::physics::MassProton);
    float fLambdaPosPiY = RecoDecay::y(std::array{lambda.pxpos(), lambda.pypos(), lambda.pzpos()}, o2::constants::physics::MassPionCharged);
    float fLambdaNegPrY = RecoDecay::y(std::array{lambda.pxneg(), lambda.pyneg(), lambda.pzneg()}, o2::constants::physics::MassProton);
    float fLambdaNegPiY = RecoDecay::y(std::array{lambda.pxneg(), lambda.pyneg(), lambda.pzneg()}, o2::constants::physics::MassPionCharged);
    int fLambdaPosITSCls = posTrackLambda.itsNCls();
    int fLambdaNegITSCls = negTrackLambda.itsNCls();
    uint32_t fLambdaPosITSClSize = posTrackLambda.itsClusterSizes();
    uint32_t fLambdaNegITSClSize = negTrackLambda.itsClusterSizes();
    uint8_t fLambdaV0Type = lambda.v0Type();

    // Filling TTree for ML analysis
    sigma0cores(fSigmapT, fSigmaMass, fSigmaRap);

    sigmaPhotonExtras(fPhotonPt, fPhotonMass, fPhotonQt, fPhotonAlpha, fPhotonRadius,
                      fPhotonCosPA, fPhotonDCADau, fPhotonDCANegPV, fPhotonDCAPosPV, fPhotonZconv,
                      fPhotonEta, fPhotonY, fPhotonPosTPCNSigma, fPhotonNegTPCNSigma, fPhotonPosTPCCrossedRows,
                      fPhotonNegTPCCrossedRows, fPhotonPosPt, fPhotonNegPt, fPhotonPosEta,
                      fPhotonNegEta, fPhotonPosY, fPhotonNegY, fPhotonPsiPair,
                      fPhotonPosITSCls, fPhotonNegITSCls, fPhotonPosITSClSize, fPhotonNegITSClSize,
                      fPhotonV0Type, GammaBDTScore);

    sigmaLambdaExtras(fLambdaPt, fLambdaMass, fAntiLambdaMass, fLambdaQt, fLambdaAlpha,
                      fLambdaRadius, fLambdaCosPA, fLambdaDCADau, fLambdaDCANegPV,
                      fLambdaDCAPosPV, fLambdaEta, fLambdaY, fLambdaPosPrTPCNSigma,
                      fLambdaPosPiTPCNSigma, fLambdaNegPrTPCNSigma, fLambdaNegPiTPCNSigma, fLambdaPosTPCCrossedRows,
                      fLambdaNegTPCCrossedRows, fLambdaPosPt, fLambdaNegPt, fLambdaPosEta,
                      fLambdaNegEta, fLambdaPosPrY, fLambdaPosPiY, fLambdaNegPrY, fLambdaNegPiY,
                      fLambdaPosITSCls, fLambdaNegITSCls, fLambdaPosITSClSize, fLambdaNegITSClSize,
                      fLambdaV0Type, LambdaBDTScore, AntiLambdaBDTScore);
  }

  void processMonteCarlo(soa::Join<aod::StraCollisions, aod::StraCents> const& collisions, V0DerivedMCDatas const& V0s)
  {
    for (const auto& coll : collisions) {
      // Do analysis with collision-grouped V0s, retain full collision information
      const uint64_t collIdx = coll.globalIndex();
      auto V0Table_thisCollision = V0s.sliceBy(perCollisionMCDerived, collIdx);

      // V0 table sliced
      for (auto& gamma : V0Table_thisCollision) {    // selecting photons from Sigma0

        float centrality = coll.centFT0C();

        // Auxiliary histograms:
        if (gamma.pdgCode() == 22) {
          float GammaY = TMath::Abs(RecoDecay::y(std::array{gamma.px(), gamma.py(), gamma.pz()}, o2::constants::physics::MassGamma));

          if (GammaY < 0.5) {                                                                                                                // rapidity selection
            histos.fill(HIST("Efficiency/h2dPtVsCentrality_GammaAll"), centrality, gamma.pt());                                              // isgamma
            histos.fill(HIST("Efficiency/h2dGammaPtResolution"), gamma.pt(), gamma.pt() - RecoDecay::pt(array{gamma.pxMC(), gamma.pyMC()})); // pT resolution

            if (gamma.pdgCodeMother() == 3212) {
              histos.fill(HIST("Efficiency/h2dPtVsCentrality_GammaSigma0"), centrality, gamma.pt()); // isgamma from sigma
            }
            if (gamma.pdgCodeMother() == -3212) {
              histos.fill(HIST("Efficiency/h2dPtVsCentrality_GammaAntiSigma0"), centrality, gamma.pt()); // isgamma from sigma
            }
          }
        }
        if (gamma.pdgCode() == 3122) { // Is Lambda
          float LambdaY = TMath::Abs(RecoDecay::y(std::array{gamma.px(), gamma.py(), gamma.pz()}, o2::constants::physics::MassLambda));
          if (LambdaY < 0.5) { // rapidity selection
            histos.fill(HIST("Efficiency/h2dPtVsCentrality_LambdaAll"), centrality, gamma.pt());
            histos.fill(HIST("Efficiency/h2dLambdaPtResolution"), gamma.pt(), gamma.pt() - RecoDecay::pt(array{gamma.pxMC(), gamma.pyMC()})); // pT resolution
            if (gamma.pdgCodeMother() == 3212) {
              histos.fill(HIST("Efficiency/h2dPtVsCentrality_LambdaSigma0"), centrality, gamma.pt());
            }
          }
        }
        if (gamma.pdgCode() == -3122) { // Is AntiLambda
          float AntiLambdaY = TMath::Abs(RecoDecay::y(std::array{gamma.px(), gamma.py(), gamma.pz()}, o2::constants::physics::MassLambda));
          if (AntiLambdaY < 0.5) { // rapidity selection
            histos.fill(HIST("Efficiency/h2dPtVsCentrality_AntiLambdaAll"), centrality, gamma.pt());
            if (gamma.pdgCodeMother() == -3212) {
              histos.fill(HIST("Efficiency/h2dPtVsCentrality_LambdaAntiSigma0"), centrality, gamma.pt()); // isantilambda from antisigma
            }
          }
        }

        for (auto& lambda : V0Table_thisCollision) { // selecting lambdas from Sigma0

          // Sigma0 candidate properties
          std::array<float, 3> pVecPhotons{gamma.px(), gamma.py(), gamma.pz()};
          std::array<float, 3> pVecLambda{lambda.px(), lambda.py(), lambda.pz()};
          auto arrMom = std::array{pVecPhotons, pVecLambda};
          float SigmaMass = RecoDecay::m(arrMom, std::array{o2::constants::physics::MassPhoton, o2::constants::physics::MassLambda0});
          float SigmapT = RecoDecay::pt(array{gamma.px() + lambda.px(), gamma.py() + lambda.py()});
          float SigmaY = TMath::Abs(RecoDecay::y(std::array{gamma.px() + lambda.px(), gamma.py() + lambda.py(), gamma.pz() + lambda.pz()}, o2::constants::physics::MassSigma0));

          histos.fill(HIST("h3dMassSigmasAll"), centrality, SigmapT, SigmaMass);

          if ((gamma.pdgCode() == 22) && (gamma.pdgCodeMother() == 3212) && (lambda.pdgCode() == 3122) && (lambda.pdgCodeMother() == 3212) && (gamma.motherMCPartId() == lambda.motherMCPartId()) && (SigmaY < 0.5)) {
            histos.fill(HIST("Efficiency/h2dPtVsCentrality_Sigma0All"), centrality, RecoDecay::pt(array{gamma.px() + lambda.px(), gamma.py() + lambda.py()}));
            histos.fill(HIST("Efficiency/h2dSigmaPtVsLambdaPt"), SigmapT, lambda.pt());
            histos.fill(HIST("Efficiency/h2dSigmaPtVsGammaPt"), SigmapT, gamma.pt());
          }
          if ((gamma.pdgCode() == 22) && (gamma.pdgCodeMother() == -3212) && (lambda.pdgCode() == -3122) && (lambda.pdgCodeMother() == -3212) && (gamma.motherMCPartId() == lambda.motherMCPartId()) && (SigmaY < 0.5))
            histos.fill(HIST("Efficiency/h2dPtVsCentrality_AntiSigma0All"), centrality, SigmapT);

          if (!processSigmaCandidate(lambda, gamma)) // basic selection
            continue;

          bool fIsSigma = false;
          bool fIsAntiSigma = false;
          histos.fill(HIST("h3dMassSigmasAfterSel"), centrality, SigmapT, SigmaMass);
          if ((gamma.pdgCode() == 22) && (gamma.pdgCodeMother() == 3212) && (lambda.pdgCode() == 3122) && (lambda.pdgCodeMother() == 3212) && (gamma.motherMCPartId() == lambda.motherMCPartId())) {
            fIsSigma = true;
            histos.fill(HIST("Efficiency/h2dPtVsCentrality_Sigma0AfterSel"), centrality, RecoDecay::pt(array{gamma.px() + lambda.px(), gamma.py() + lambda.py()}));
          }
          if ((gamma.pdgCode() == 22) && (gamma.pdgCodeMother() == -3212) && (lambda.pdgCode() == -3122) && (lambda.pdgCodeMother() == -3212) && (gamma.motherMCPartId() == lambda.motherMCPartId())) {
            fIsAntiSigma = true;
            histos.fill(HIST("Efficiency/h2dPtVsCentrality_AntiSigma0AfterSel"), centrality, RecoDecay::pt(array{gamma.px() + lambda.px(), gamma.py() + lambda.py()}));
          }
          sigma0mccores(fIsSigma, fIsAntiSigma);
        }
      }
    }
  }

  void processSTDSelection(soa::Join<aod::StraCollisions, aod::StraCents> const& collisions, V0StandardDerivedDatas const& V0s, dauTracks const&)
  {
    for (const auto& coll : collisions) {
      // Do analysis with collision-grouped V0s, retain full collision information
      const uint64_t collIdx = coll.globalIndex();
      auto V0Table_thisCollision = V0s.sliceBy(perCollisionSTDDerived, collIdx);

      histos.fill(HIST("hEventVertexZ"), coll.posZ());
      histos.fill(HIST("hEventCentrality"), coll.centFT0C());
      sigma0Coll(coll.posX(), coll.posY(), coll.posZ(), coll.centFT0M(), coll.centFT0A(), coll.centFT0C(), coll.centFV0A());

      // V0 table sliced
      for (auto& gamma : V0Table_thisCollision) {    // selecting photons from Sigma0
        for (auto& lambda : V0Table_thisCollision) { // selecting lambdas from Sigma0
          if (!processSigmaCandidate(lambda, gamma)) // applying selection for reconstruction
            continue;

          nSigmaCandidates++;
          if (nSigmaCandidates % 5000 == 0) {
            LOG(info) << "Sigma0 Candidates built: " << nSigmaCandidates;
          }

          sigma0CollRefs(collIdx);
          fillTables(lambda, gamma); // filling tables with accepted candidates
        }
      }
    }
  }

  void processMLSelection(soa::Join<aod::StraCollisions, aod::StraCents> const& collisions, V0MLDerivedDatas const& V0s, dauTracks const&)
  {
    for (const auto& coll : collisions) {
      // Do analysis with collision-grouped V0s, retain full collision information
      const uint64_t collIdx = coll.globalIndex();
      auto V0Table_thisCollision = V0s.sliceBy(perCollisionMLDerived, collIdx);

      histos.fill(HIST("hEventVertexZ"), coll.posZ());
      sigma0Coll(coll.posX(), coll.posY(), coll.posZ(), coll.centFT0M(), coll.centFT0A(), coll.centFT0C(), coll.centFV0A());

      // V0 table sliced
      for (auto& gamma : V0Table_thisCollision) {    // selecting photons from Sigma0
        for (auto& lambda : V0Table_thisCollision) { // selecting lambdas from Sigma0
          if (!processSigmaCandidate(lambda, gamma))
            continue;

          nSigmaCandidates++;
          if (nSigmaCandidates % 5000 == 0) {
            LOG(info) << "Sigma0 Candidates built: " << nSigmaCandidates;
          }
          sigma0CollRefs(collIdx);
          fillTables(lambda, gamma); // filling tables with accepted candidates
        }
      }
    }
  }
  PROCESS_SWITCH(sigma0builder, processMonteCarlo, "Fill sigma0 MC table", false);
  PROCESS_SWITCH(sigma0builder, processSTDSelection, "Select gammas and lambdas with standard cuts", true);
  PROCESS_SWITCH(sigma0builder, processMLSelection, "Select gammas and lambdas with ML", false);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{adaptAnalysisTask<sigma0builder>(cfgc)};
}
