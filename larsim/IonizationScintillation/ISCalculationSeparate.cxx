////////////////////////////////////////////////////////////////////////
/// \file  ISCalcuationSeparate.cxx
/// \brief Interface to algorithm class for calculating ionization electrons
///        and scintillation photons using separate algorithms for each
///
/// Wes, 18Feb2018: this is a copy of the original, for standalone purposes
///
///
/// \version $Id:  $
/// \author  brebel@fnal.gov
////////////////////////////////////////////////////////////////////////
#include "CLHEP/Vector/ThreeVector.h"

#include "larsim/IonizationScintillation/ISCalculationSeparate.h"
#include "lardataalg/DetectorInfo/LArProperties.h"
#include "lardataalg/DetectorInfo/DetectorProperties.h"
#include "larsim/Simulation/LArG4Parameters.h"
#include "larevt/SpaceCharge/SpaceCharge.h"

#include "messagefacility/MessageLogger/MessageLogger.h"
#include "cetlib/exception.h"

#include "lardataobj/Simulation/SimEnergyDeposit.h"

namespace larg4{

  //----------------------------------------------------------------------------
  ISCalculationSeparate::ISCalculationSeparate()
  {
  }

  //----------------------------------------------------------------------------
  ISCalculationSeparate::~ISCalculationSeparate()
  {
  }

  //----------------------------------------------------------------------------
  void ISCalculationSeparate::Initialize(const detinfo::LArProperties* larp,   
					 const detinfo::DetectorProperties* detp,
					 const sim::LArG4Parameters* lgp,
					 const spacecharge::SpaceCharge* sce)
  {
    fLArProp = larp;
    fSCE = sce;
    fDetProp = detp;
    fLArG4Prop = lgp;
    
    // \todo get scintillation yield from LArG4Parameters or LArProperties
    fScintYieldFactor  = 1.;

    // the recombination coefficient is in g/(MeVcm^2), but
    // we report energy depositions in MeV/cm, need to divide
    // Recombk from the LArG4Parameters service by the density
    // of the argon we got above.
    fRecombA             = (double)lgp->RecombA();
    fRecombk             = (double)lgp->Recombk()/detp->Density(detp->Temperature());
    fModBoxA             = (double)lgp->ModBoxA();
    fModBoxB             = (double)lgp->ModBoxB()/detp->Density(detp->Temperature());
    fUseModBoxRecomb     = (bool)lgp->UseModBoxRecomb();  

    this->Reset();
    
    return;
  }

  //----------------------------------------------------------------------------
  void ISCalculationSeparate::Reset()
  {
    fEnergyDeposit   = 0.;
    fNumScintPhotons = 0.;
    fNumIonElectrons = 0.;

    return;
  }

  //----------------------------------------------------------------------------
  // fNumIonElectrons returns a value that is not corrected for life time effects
  void ISCalculationSeparate::CalculateIonization(float e, float ds,
						  float x, float y, float z){
    double recomb = 0.;
    double dEdx   = e/ds;
    double EFieldStep = EFieldAtStep(fDetProp->Efield(),x,y,z);
    
    // Guard against spurious values of dE/dx. Note: assumes density of LAr
    if(dEdx < 1.) dEdx = 1.;
    
    if(fUseModBoxRecomb) {
      if(ds){
	double Xi = fModBoxB * dEdx / EFieldStep;
	recomb = log(fModBoxA + Xi) / Xi;
      }
      else 
	recomb = 0;
    }
    else{
      recomb = fLArG4Prop->RecombA() / (1. + dEdx * fRecombk / EFieldStep);
    }
    
    
    // 1.e-3 converts fEnergyDeposit to GeV
    fNumIonElectrons = fLArG4Prop->GeVToElectrons() * 1.e-3 * e * recomb;

    LOG_DEBUG("ISCalculationSeparate") 
      << " Electrons produced for " << fEnergyDeposit 
      << " MeV deposited with "     << recomb 
      << " recombination: "         << fNumIonElectrons << std::endl; 
  }


  //----------------------------------------------------------------------------
  void ISCalculationSeparate::CalculateIonization(sim::SimEnergyDeposit const& edep){
    CalculateIonization(edep.Energy(),edep.StepLength(),
			edep.MidPointX(),edep.MidPointY(),edep.MidPointZ());
  }
  
  //----------------------------------------------------------------------------
  void ISCalculationSeparate::CalculateScintillation(float e, int pdg)
  {
    double scintYield = fLArProp->ScintYield(true);
    if(fLArProp->ScintByParticleType()){

      LOG_DEBUG("ISCalculationSeparate") << "scintillating by particle type";

      switch(pdg) {

      case 2212:
	scintYield = fLArProp->ProtonScintYield(true);
	break;
      case 13:
      case -13:
	scintYield = fLArProp->MuonScintYield(true);
	break;
      case 211:
      case -211:
	scintYield = fLArProp->PionScintYield(true);
	break;
      case 321:
      case -321:
	scintYield = fLArProp->KaonScintYield(true);
	break;
      case 1000020040:
	scintYield = fLArProp->AlphaScintYield(true);
	break;
      case 11:
      case -11:
      case 22:
	scintYield = fLArProp->ElectronScintYield(true);
	break;
      default:
	scintYield = fLArProp->ElectronScintYield(true);

      }

      fNumScintPhotons =  scintYield * e;
    }
    else
      fNumScintPhotons = fScintYieldFactor * scintYield * e;
    
  }

  //----------------------------------------------------------------------------
  void ISCalculationSeparate::CalculateScintillation(sim::SimEnergyDeposit const& edep)
  {
    CalculateScintillation(edep.Energy(),edep.PdgCode());
  }

  //----------------------------------------------------------------------------
  void ISCalculationSeparate::CalculateIonizationAndScintillation(sim::SimEnergyDeposit const& edep)
  {
    fEnergyDeposit = edep.Energy();
    CalculateIonization(edep);
    CalculateScintillation(edep);
  }

  double ISCalculationSeparate::EFieldAtStep(double efield, sim::SimEnergyDeposit const& edep)
  {
    return EFieldAtStep(efield,
			edep.MidPointX(),edep.MidPointY(),edep.MidPointZ());
  }
  
  double ISCalculationSeparate::EFieldAtStep(double efield, float x, float y, float z)
  {
    double EField = efield;
    if (fSCE->EnableSimEfieldSCE())
      {
        fEfieldOffsets = fSCE->GetEfieldOffsets(geo::Point_t{x,y,z});
        EField = std::sqrt( (efield + efield*fEfieldOffsets.X())*(efield + efield*fEfieldOffsets.X()) +
			    (efield*fEfieldOffsets.Y()+efield*fEfieldOffsets.Y()) +
			    (efield*fEfieldOffsets.Z()+efield*fEfieldOffsets.Z()) );
      }
    return EField;
  }

}// namespace
