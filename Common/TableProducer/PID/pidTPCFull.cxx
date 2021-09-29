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

///
/// \file   pidTPCFull.cxx
/// \author Nicolo' Jacazio
/// \brief  Task to produce PID tables for TPC split for each particle.
///         Only the tables for the mass hypotheses requested are filled, the others are sent empty.
///

// O2 includes
#include "Framework/AnalysisTask.h"
#include "Framework/HistogramRegistry.h"
#include "Framework/RunningWorkflowInfo.h"
#include "ReconstructionDataFormats/Track.h"
#include <CCDB/BasicCCDBManager.h>
#include "Common/Core/PID/PIDResponse.h"
#include "Common/Core/PID/PIDTPC.h"
#include "Common/DataModel/TrackSelectionTables.h"

using namespace o2;
using namespace o2::framework;
using namespace o2::pid;
using namespace o2::framework::expressions;
using namespace o2::track;

void customize(std::vector<o2::framework::ConfigParamSpec>& workflowOptions)
{
  std::vector<ConfigParamSpec> options{{"add-qa", VariantType::Int, 0, {"Produce TPC PID QA histograms"}}};
  std::swap(workflowOptions, options);
}

#include "Framework/runDataProcessing.h"

struct tpcPidFull {
  using Trks = soa::Join<aod::Tracks, aod::TracksExtra>;
  using Coll = aod::Collisions;
  // Tables to produce
  Produces<o2::aod::pidTPCFullEl> tablePIDEl;
  Produces<o2::aod::pidTPCFullMu> tablePIDMu;
  Produces<o2::aod::pidTPCFullPi> tablePIDPi;
  Produces<o2::aod::pidTPCFullKa> tablePIDKa;
  Produces<o2::aod::pidTPCFullPr> tablePIDPr;
  Produces<o2::aod::pidTPCFullDe> tablePIDDe;
  Produces<o2::aod::pidTPCFullTr> tablePIDTr;
  Produces<o2::aod::pidTPCFullHe> tablePIDHe;
  Produces<o2::aod::pidTPCFullAl> tablePIDAl;
  // Detector response and input parameters
  DetectorResponse response;
  Service<o2::ccdb::BasicCCDBManager> ccdb;
  Configurable<std::string> paramfile{"param-file", "", "Path to the parametrization object, if emtpy the parametrization is not taken from file"};
  Configurable<std::string> signalname{"param-signal", "BetheBloch", "Name of the parametrization for the expected signal, used in both file and CCDB mode"};
  Configurable<std::string> sigmaname{"param-sigma", "TPCReso", "Name of the parametrization for the expected sigma, used in both file and CCDB mode"};
  Configurable<std::string> url{"ccdb-url", "http://alice-ccdb.cern.ch", "url of the ccdb repository"};
  Configurable<std::string> ccdbPath{"ccdbPath", "Analysis/PID/TPC", "Path of the TPC parametrization on the CCDB"};
  Configurable<long> timestamp{"ccdb-timestamp", -1, "timestamp of the object"};
  // Configuration flags to include and exclude particle hypotheses
  Configurable<int> pidEl{"pid-el", -1, {"Produce PID information for the Electron mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};
  Configurable<int> pidMu{"pid-mu", -1, {"Produce PID information for the Muon mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};
  Configurable<int> pidPi{"pid-pi", -1, {"Produce PID information for the Pion mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};
  Configurable<int> pidKa{"pid-ka", -1, {"Produce PID information for the Kaon mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};
  Configurable<int> pidPr{"pid-pr", -1, {"Produce PID information for the Proton mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};
  Configurable<int> pidDe{"pid-de", -1, {"Produce PID information for the Deuterons mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};
  Configurable<int> pidTr{"pid-tr", -1, {"Produce PID information for the Triton mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};
  Configurable<int> pidHe{"pid-he", -1, {"Produce PID information for the Helium3 mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};
  Configurable<int> pidAl{"pid-al", -1, {"Produce PID information for the Alpha mass hypothesis, overrides the automatic setup: the corresponding table can be set off (0) or on (1)"}};

  void init(o2::framework::InitContext& initContext)
  {
    // Checking the tables are requested in the workflow and enabling them
    auto& workflows = initContext.services().get<RunningWorkflowInfo const>();
    for (DeviceSpec device : workflows.devices) {
      for (auto input : device.inputs) {
        auto enableFlag = [&input](const std::string particle, Configurable<int>& flag) {
          const std::string table = "pidTPCFull" + particle;
          if (input.matcher.binding == table) {
            if (flag < 0) {
              flag.value = 1;
              LOG(info) << "Auto-enabling table: " + table;
            } else if (flag > 0) {
              flag.value = 1;
              LOG(info) << "Table enabled: " + table;
            } else {
              LOG(info) << "Table disabled: " + table;
            }
          }
        };
        enableFlag("El", pidEl);
        enableFlag("Mu", pidMu);
        enableFlag("Pi", pidPi);
        enableFlag("Ka", pidKa);
        enableFlag("Pr", pidPr);
        enableFlag("De", pidDe);
        enableFlag("Tr", pidTr);
        enableFlag("He", pidHe);
        enableFlag("Al", pidAl);
      }
    }
    // Getting the parametrization parameters
    ccdb->setURL(url.value);
    ccdb->setTimestamp(timestamp.value);
    ccdb->setCaching(true);
    ccdb->setLocalObjectValidityChecking();
    // Not later than now objects
    ccdb->setCreatedNotAfter(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    //
    const std::string fname = paramfile.value;
    if (!fname.empty()) { // Loading the parametrization from file
      LOG(info) << "Loading exp. signal parametrization from file" << fname << ", using param: " << signalname.value;
      response.LoadParamFromFile(fname.data(), signalname.value, DetectorResponse::kSignal);

      LOG(info) << "Loading exp. sigma parametrization from file" << fname << ", using param: " << sigmaname.value;
      response.LoadParamFromFile(fname.data(), sigmaname.value, DetectorResponse::kSigma);
    } else { // Loading it from CCDB
      std::string path = ccdbPath.value + "/" + signalname.value;
      LOG(info) << "Loading exp. signal parametrization from CCDB, using path: " << path << " for timestamp " << timestamp.value;
      response.LoadParam(DetectorResponse::kSignal, ccdb->getForTimeStamp<Parametrization>(path, timestamp.value));

      path = ccdbPath.value + "/" + sigmaname.value;
      LOG(info) << "Loading exp. sigma parametrization from CCDB, using path: " << path << " for timestamp " << timestamp.value;
      response.LoadParam(DetectorResponse::kSigma, ccdb->getForTimeStamp<Parametrization>(path, timestamp.value));
    }
  }

  template <o2::track::PID::ID pid>
  using ResponseImplementation = o2::pid::tpc::ELoss<Trks::iterator, pid>;
  void process(Coll const& collisions, Trks const& tracks)
  {
    constexpr auto responseEl = ResponseImplementation<PID::Electron>();
    constexpr auto responseMu = ResponseImplementation<PID::Muon>();
    constexpr auto responsePi = ResponseImplementation<PID::Pion>();
    constexpr auto responseKa = ResponseImplementation<PID::Kaon>();
    constexpr auto responsePr = ResponseImplementation<PID::Proton>();
    constexpr auto responseDe = ResponseImplementation<PID::Deuteron>();
    constexpr auto responseTr = ResponseImplementation<PID::Triton>();
    constexpr auto responseHe = ResponseImplementation<PID::Helium3>();
    constexpr auto responseAl = ResponseImplementation<PID::Alpha>();

    // Check and fill enabled tables
    auto makeTable = [&tracks](const Configurable<int>& flag, auto& table, const DetectorResponse& response, const auto& responsePID) {
      if (flag.value == 1) {
        // Prepare memory for enabled tables
        table.reserve(tracks.size());
        for (auto const& trk : tracks) { // Loop on Tracks
          table(responsePID.GetExpectedSigma(response, trk),
                responsePID.GetSeparation(response, trk));
        }
      }
    };
    makeTable(pidEl, tablePIDEl, response, responseEl);
    makeTable(pidMu, tablePIDMu, response, responseMu);
    makeTable(pidPi, tablePIDPi, response, responsePi);
    makeTable(pidKa, tablePIDKa, response, responseKa);
    makeTable(pidPr, tablePIDPr, response, responsePr);
    makeTable(pidDe, tablePIDDe, response, responseDe);
    makeTable(pidTr, tablePIDTr, response, responseTr);
    makeTable(pidHe, tablePIDHe, response, responseHe);
    makeTable(pidAl, tablePIDAl, response, responseAl);
  }
};

struct tpcPidFullQa {
  static constexpr int Np = 9;
  static constexpr const char* pT[Np] = {"e", "#mu", "#pi", "K", "p", "d", "t", "^{3}He", "#alpha"};
  static constexpr std::string_view hexpected[Np] = {"expected/El", "expected/Mu", "expected/Pi",
                                                     "expected/Ka", "expected/Pr", "expected/De",
                                                     "expected/Tr", "expected/He", "expected/Al"};
  static constexpr std::string_view hexpected_diff[Np] = {"expected_diff/El", "expected_diff/Mu", "expected_diff/Pi",
                                                          "expected_diff/Ka", "expected_diff/Pr", "expected_diff/De",
                                                          "expected_diff/Tr", "expected_diff/He", "expected_diff/Al"};
  static constexpr std::string_view hnsigma[Np] = {"nsigma/El", "nsigma/Mu", "nsigma/Pi",
                                                   "nsigma/Ka", "nsigma/Pr", "nsigma/De",
                                                   "nsigma/Tr", "nsigma/He", "nsigma/Al"};
  HistogramRegistry histos{"Histos", {}, OutputObjHandlingPolicy::QAObject};

  Configurable<int> logAxis{"logAxis", 0, "Flag to use a log momentum axis"};
  Configurable<int> nBinsP{"nBinsP", 400, "Number of bins for the momentum"};
  Configurable<float> minP{"minP", 0, "Minimum momentum in range"};
  Configurable<float> maxP{"maxP", 20, "Maximum momentum in range"};
  Configurable<int> nBinsDelta{"nBinsDelta", 200, "Number of bins for the Delta"};
  Configurable<float> minDelta{"minDelta", -1000.f, "Minimum Delta in range"};
  Configurable<float> maxDelta{"maxDelta", 1000.f, "Maximum Delta in range"};
  Configurable<int> nBinsNSigma{"nBinsNSigma", 200, "Number of bins for the NSigma"};
  Configurable<float> minNSigma{"minNSigma", -10.f, "Minimum NSigma in range"};
  Configurable<float> maxNSigma{"maxNSigma", 10.f, "Maximum NSigma in range"};

  template <uint8_t i>
  void addParticleHistos()
  {
    AxisSpec pAxis{nBinsP, minP, maxP, "#it{p} (GeV/#it{c})"};
    if (logAxis) {
      pAxis.makeLogaritmic();
    }

    // Exp signal
    const AxisSpec expAxis{1000, 0, 1000, Form("d#it{E}/d#it{x}_(%s) A.U.", pT[i])};
    histos.add(hexpected[i].data(), "", kTH2F, {pAxis, expAxis});

    // Signal - Expected signal
    const AxisSpec deltaAxis{nBinsDelta, minDelta, maxDelta, Form("d#it{E}/d#it{x} - d#it{E}/d#it{x}(%s)", pT[i])};
    histos.add(hexpected_diff[i].data(), "", kTH2F, {pAxis, deltaAxis});

    // NSigma
    const AxisSpec nSigmaAxis{nBinsNSigma, minNSigma, maxNSigma, Form("N_{#sigma}^{TPC}(%s)", pT[i])};
    histos.add(hnsigma[i].data(), "", kTH2F, {pAxis, nSigmaAxis});
  }

  void init(o2::framework::InitContext&)
  {

    AxisSpec pAxis{nBinsP, minP, maxP, "#it{p} (GeV/#it{c})"};
    if (logAxis) {
      pAxis.makeLogaritmic();
    }
    const AxisSpec vtxZAxis{100, -20, 20, "Vtx_{z} (cm)"};
    const AxisSpec dedxAxis{1000, 0, 1000, "d#it{E}/d#it{x} A.U."};

    // Event properties
    histos.add("event/vertexz", "", kTH1F, {vtxZAxis});
    histos.add("event/tpcsignal", "", kTH2F, {pAxis, dedxAxis});

    addParticleHistos<0>();
    addParticleHistos<1>();
    addParticleHistos<2>();
    addParticleHistos<3>();
    addParticleHistos<4>();
    addParticleHistos<5>();
    addParticleHistos<6>();
    addParticleHistos<7>();
    addParticleHistos<8>();
  }

  template <uint8_t i, typename T>
  void fillParticleHistos(const T& t, const float mom, const float exp_diff, const float nsigma)
  {
    histos.fill(HIST(hexpected[i]), mom, t.tpcSignal() - exp_diff);
    histos.fill(HIST(hexpected_diff[i]), mom, exp_diff);
    histos.fill(HIST(hnsigma[i]), t.p(), nsigma);
  }

  void process(aod::Collision const& collision, soa::Join<aod::Tracks, aod::TracksExtra,
                                                          aod::pidTPCFullEl, aod::pidTPCFullMu, aod::pidTPCFullPi,
                                                          aod::pidTPCFullKa, aod::pidTPCFullPr, aod::pidTPCFullDe,
                                                          aod::pidTPCFullTr, aod::pidTPCFullHe, aod::pidTPCFullAl,
                                                          aod::TrackSelection> const& tracks)
  {
    histos.fill(HIST("event/vertexz"), collision.posZ());

    for (auto t : tracks) {
      // const float mom = t.p();
      const float mom = t.tpcInnerParam();
      histos.fill(HIST("event/tpcsignal"), mom, t.tpcSignal());
      //
      fillParticleHistos<0>(t, mom, t.tpcExpSignalDiffEl(), t.tpcNSigmaEl());
      fillParticleHistos<1>(t, mom, t.tpcExpSignalDiffMu(), t.tpcNSigmaMu());
      fillParticleHistos<2>(t, mom, t.tpcExpSignalDiffPi(), t.tpcNSigmaPi());
      fillParticleHistos<3>(t, mom, t.tpcExpSignalDiffKa(), t.tpcNSigmaKa());
      fillParticleHistos<4>(t, mom, t.tpcExpSignalDiffPr(), t.tpcNSigmaPr());
      fillParticleHistos<5>(t, mom, t.tpcExpSignalDiffDe(), t.tpcNSigmaDe());
      fillParticleHistos<6>(t, mom, t.tpcExpSignalDiffTr(), t.tpcNSigmaTr());
      fillParticleHistos<7>(t, mom, t.tpcExpSignalDiffHe(), t.tpcNSigmaHe());
      fillParticleHistos<8>(t, mom, t.tpcExpSignalDiffAl(), t.tpcNSigmaAl());
    }
  }
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  auto workflow = WorkflowSpec{adaptAnalysisTask<tpcPidFull>(cfgc)};
  if (cfgc.options().get<int>("add-qa")) {
    workflow.push_back(adaptAnalysisTask<tpcPidFullQa>(cfgc));
  }
  return workflow;
}
