////////////////////////////////////////////////////////////////////////
// Class:       NucleonDecay
// Module Type: producer
// GENIE nucleon decay generator
//
// Converted from gNucleonDecayEvGen.cxx by
// tjyang@fnal.gov
//
// 2016 PDG numbering scheme in pp.8-10 of http://www-pdg.lbl.gov/2016/listings/rpp2016-list-p.pdf (tau1 through tau60)
//
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

// GENIE includes
#include "Algorithm/AlgFactory.h"
#include "EVGCore/EventRecordVisitorI.h"
#include "EVGCore/EventRecord.h"
#include "NucleonDecay/NucleonDecayMode.h"
#include "NucleonDecay/NucleonDecayUtils.h"
#include "PDG/PDGLibrary.h"
#include "GHEP/GHepParticle.h"
#include "Utils/AppInit.h"

// larsoft includes
#include "nusimdata/SimulationBase/MCTruth.h"
#include "nusimdata/SimulationBase/MCParticle.h"
#include "nusimdata/SimulationBase/MCNeutrino.h"
#include "nutools/EventGeneratorBase/evgenbase.h"
#include "larcore/Geometry/Geometry.h"
#include "larcoreobj/SummaryData/RunData.h"
#include "nutools/RandomUtils/NuRandomService.h"

// c++ includes
#include <memory>
#include <string>

#include "CLHEP/Random/RandFlat.h"

namespace evgen {
  class NucleonDecay;
}

class evgen::NucleonDecay : public art::EDProducer {
public:
  explicit NucleonDecay(fhicl::ParameterSet const & p);
  // The destructor generated by the compiler is fine for classes
  // without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  NucleonDecay(NucleonDecay const &) = delete;
  NucleonDecay(NucleonDecay &&) = delete;
  NucleonDecay & operator = (NucleonDecay const &) = delete;
  NucleonDecay & operator = (NucleonDecay &&) = delete;

  // Required functions.
  void produce(art::Event & e) override;

  // Selected optional functions.
  void beginJob() override;
  void beginRun(art::Run& run) override;

private:

  // Declare member data here.
  const genie::EventRecordVisitorI * mcgen;
  genie::NucleonDecayMode_t gOptDecayMode    = genie::kNDNull;             // nucleon decay mode
  int dpdg = 0;
};


evgen::NucleonDecay::NucleonDecay(fhicl::ParameterSet const & p)
  : art::EDProducer{p}
{
  genie::PDGLibrary::Instance(); //Ensure Messenger is started first in GENIE.

  string sname   = "genie::EventGenerator";
  string sconfig = "NucleonDecay";
  genie::AlgFactory * algf = genie::AlgFactory::Instance();
  mcgen =
    dynamic_cast<const genie::EventRecordVisitorI *> (algf->GetAlgorithm(sname,sconfig));
  if(!mcgen) {
    throw cet::exception("NucleonDecay") << "Couldn't instantiate the nucleon decay generator"; 
  }
  int fDecayMode = p.get<int>("DecayMode");
  gOptDecayMode = (genie::NucleonDecayMode_t) fDecayMode;

  if (p.get<int>("DecayedNucleon") > 0 ){
    dpdg = p.get<int>("DecayedNucleon");
  }
  else{
    dpdg = genie::utils::nucleon_decay::DecayedNucleonPdgCode(gOptDecayMode);
  }

  produces< std::vector<simb::MCTruth> >();
  produces< sumdata::RunData, art::InRun >();
  
  // create a default random engine; obtain the random seed from NuRandomService,
  // unless overridden in configuration with key "Seed"
  art::ServiceHandle<rndm::NuRandomService>()
    ->createEngine(*this, p, "Seed");

  unsigned int seed = art::ServiceHandle<rndm::NuRandomService>()->getSeed();
  genie::utils::app_init::RandGen(seed);
}

void evgen::NucleonDecay::produce(art::Event & e)
{
  // Implementation of required member function here.
  genie::EventRecord * event = new genie::EventRecord;
  int target = 1000180400;  //Only use argon target
  int decay  = (int)gOptDecayMode;
  genie::Interaction * interaction = genie::Interaction::NDecay(target,decay,dpdg);
  event->AttachSummary(interaction);
  
  // Simulate decay     
  mcgen->ProcessEventRecord(event);

//  genie::Interaction *inter = event->Summary();
//  const genie::InitialState &initState = inter->InitState();
//  std::cout<<"initState = "<<initState.AsString()<<std::endl;
//  const genie::ProcessInfo &procInfo = inter->ProcInfo();
//  std::cout<<"procInfo = "<<procInfo.AsString()<<std::endl;
  LOG_DEBUG("NucleonDecay")
    << "Generated event: " << *event;

  std::unique_ptr< std::vector<simb::MCTruth> > truthcol(new std::vector<simb::MCTruth>);
  simb::MCTruth truth;
  
  art::ServiceHandle<geo::Geometry> geo;
  art::ServiceHandle<art::RandomNumberGenerator> rng;
  CLHEP::HepRandomEngine &engine = rng->getEngine(art::ScheduleID::first(),
                                                  moduleDescription().moduleLabel());
  CLHEP::RandFlat flat(engine);

  // Find boundary of active volume
  double minx = 1e9;
  double maxx = -1e9;
  double miny = 1e9;
  double maxy = -1e9;
  double minz = 1e9;
  double maxz = -1e9;
  for (size_t i = 0; i<geo->NTPC(); ++i){
    const geo::TPCGeo &tpc = geo->TPC(i);
    if (minx>tpc.MinX()) minx = tpc.MinX();
    if (maxx<tpc.MaxX()) maxx = tpc.MaxX();
    if (miny>tpc.MinY()) miny = tpc.MinY();
    if (maxy<tpc.MaxY()) maxy = tpc.MaxY();
    if (minz>tpc.MinZ()) minz = tpc.MinZ();
    if (maxz<tpc.MaxZ()) maxz = tpc.MaxZ();
  }

  // Assign vertice position
  double X0 = flat.fire( minx, maxx );
  double Y0 = flat.fire( miny, maxy );
  double Z0 = flat.fire( minz, maxz );

  TIter partitr(event);
  genie::GHepParticle *part = 0;
  // GHepParticles return units of GeV/c for p.  the V_i are all in fermis
  // and are relative to the center of the struck nucleus.
  // add the vertex X/Y/Z to the V_i for status codes 0 and 1
  int trackid = 0;
  std::string primary("primary");
  
  while( (part = dynamic_cast<genie::GHepParticle *>(partitr.Next())) ){
    
    simb::MCParticle tpart(trackid, 
                           part->Pdg(),
                           primary,
                           part->FirstMother(),
                           part->Mass(), 
                           part->Status());
    
    TLorentzVector pos(X0, Y0, Z0, 0);
    TLorentzVector mom(part->Px(), part->Py(), part->Pz(), part->E());
    tpart.AddTrajectoryPoint(pos,mom);
    if(part->PolzIsSet()) {
      TVector3 polz;
      part->GetPolarization(polz);
      tpart.SetPolarization(polz);
    }
    tpart.SetRescatter(part->RescatterCode());
    truth.Add(tpart);
    
    ++trackid;        
  }// end loop to convert GHepParticles to MCParticles
  truth.SetOrigin(simb::kUnknown);
  truthcol->push_back(truth);
  //FillHistograms(truth);  
  e.put(std::move(truthcol));

  delete event;
  return;

}

void evgen::NucleonDecay::beginRun(art::Run& run)
{
    
  // grab the geometry object to see what geometry we are using
  art::ServiceHandle<geo::Geometry> geo;
  std::unique_ptr<sumdata::RunData> runcol(new sumdata::RunData(geo->DetectorName()));
  
  run.put(std::move(runcol));
  
  return;
}


void evgen::NucleonDecay::beginJob()
{
  // Implementation of optional member function here.
}

DEFINE_ART_MODULE(evgen::NucleonDecay)
