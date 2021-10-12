#include "G4Box.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4ParticleGun.hh"
#include "G4RunManagerFactory.hh"
#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4VUserActionInitialization.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4VisExecutive.hh"
#include "QBBC.hh"
#include "Randomize.hh"

using namespace CLHEP;

class HitSimDetectorConstruction : public G4VUserDetectorConstruction {
  public:
	HitSimDetectorConstruction() {}
	virtual ~HitSimDetectorConstruction() {}

	virtual G4VPhysicalVolume *Construct() {
		G4NistManager *nist = G4NistManager::Instance();

		G4Material *mat_air		= nist->FindOrBuildMaterial("G4_AIR");
		G4Material *mat_silicon = nist->FindOrBuildMaterial("G4_Si");
		G4Material *mat_carbon	= nist->FindOrBuildMaterial("G4_C");

		G4Element *el_carbon   = nist->FindOrBuildElement("C");
		G4Element *el_oxygen   = nist->FindOrBuildElement("O");
		G4Element *el_chlorine = nist->FindOrBuildElement("Cl");
		G4Element *el_hydrogen = nist->FindOrBuildElement("H");

		double world_width		   = 10 * mm;
		double world_height		   = 10 * mm;
		double world_depth		   = 50 * mm;
		double backplate_thickness = 200 * um;
		double chip_thickness	   = 200 * um;
		double chip_gap			   = 200 * um;

		// epoxy elements C21H25ClO5
		// (https://pubchem.ncbi.nlm.nih.gov/compound/Epoxy-resin)
		G4Material *mat_epoxy = new G4Material("Epoxy", 1.1 * g / cm3, 4);
		mat_epoxy->AddElementByNumberOfAtoms(el_carbon, 21);
		mat_epoxy->AddElementByNumberOfAtoms(el_hydrogen, 25);
		mat_epoxy->AddElementByNumberOfAtoms(el_chlorine, 1);
		mat_epoxy->AddElementByNumberOfAtoms(el_oxygen, 5);

		// CFRP with 50/50 mix
		G4Material *mat_cfrp = new G4Material("CFRP", 1.9 * g / cm3, 2);
		mat_cfrp->AddMaterial(mat_epoxy, 0.5);
		mat_cfrp->AddMaterial(mat_carbon, 0.5);

		// world (air filled box, wireframe render)
		G4Box *world_box = new G4Box("World", world_width / 2, world_height / 2, world_depth / 2);
		G4LogicalVolume *world_logical = new G4LogicalVolume(world_box, mat_air, "World");
		G4VisAttributes *world_vis	   = new G4VisAttributes();
		world_vis->SetForceWireframe(true);
		world_logical->SetVisAttributes(world_vis);
		G4VPhysicalVolume *world_physical = new G4PVPlacement(
			nullptr,		 // rotation
			G4ThreeVector(), // position
			world_logical, "World", nullptr, false, 0, true);

		// CFRP backplate
		G4Box *backplate_box = new G4Box("Backplate", 5 * mm, 5 * mm, backplate_thickness / 2);
		G4LogicalVolume *backplate_logical =
			new G4LogicalVolume(backplate_box, mat_cfrp, "Backplate");
		G4VPhysicalVolume *backplate_physical = new G4PVPlacement(
			nullptr,									  // rotation
			G4ThreeVector(0, 0, backplate_thickness / 2), // position
			backplate_logical, "Backplate", world_logical, false, 0, true);

		// two chips (gap at x=0)
		auto chip_width = (world_width - chip_gap) / 2;
		G4Box *			   chip_box		 = new G4Box("Chip", chip_width / 2, world_height / 2, chip_thickness / 2);
		G4LogicalVolume *  chip_logical	 = new G4LogicalVolume(chip_box, mat_silicon, "Chip");
		for (auto &sign_x : {-1.0, 1.0}) {
			auto pos_x = sign_x * (chip_gap / 2 + chip_width / 2);
			G4VPhysicalVolume *chip_physical = new G4PVPlacement(
				nullptr,													   // rotation
				G4ThreeVector(pos_x, 0, backplate_thickness + chip_thickness / 2), // position
				chip_logical, "Chip", world_logical, false, 0, true);
		}


		return world_physical;
	}
};

class HitSimPrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
  public:
	HitSimPrimaryGeneratorAction() {
		this->fParticleGun					= new G4ParticleGun(1);
		G4ParticleTable *	  particleTable = G4ParticleTable::GetParticleTable();
		G4ParticleDefinition *particle		= particleTable->FindParticle("proton");
		this->fParticleGun->SetParticleDefinition(particle);
		this->fParticleGun->SetParticleEnergy(100 * MeV);
		this->fParticleGun->SetParticlePosition(G4ThreeVector(0, 0, -20 * mm));
		this->fParticleGun->SetParticleMomentumDirection(G4ThreeVector(0, 0, 1));
	}
	virtual ~HitSimPrimaryGeneratorAction() { delete this->fParticleGun; }

	virtual void GeneratePrimaries(G4Event *event) {
		this->fParticleGun->GeneratePrimaryVertex(event);
	}

	G4ParticleGun *fParticleGun;
};

class HitSimActionInitialization : public G4VUserActionInitialization {
  public:
	HitSimActionInitialization() {}
	virtual ~HitSimActionInitialization() {}

	virtual void Build() const { this->SetUserAction(new HitSimPrimaryGeneratorAction()); }

	virtual void BuildForMaster() const {}
};

int main(int argc, char **argv) {
	G4UIExecutive *ui = 0;
	if (argc == 1) {
		ui = new G4UIExecutive(argc, argv);
	}

	auto *runManager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Default);

	runManager->SetUserInitialization(new HitSimDetectorConstruction());
	runManager->SetUserInitialization(new QBBC());
	runManager->SetUserInitialization(new HitSimActionInitialization());

	runManager->Initialize();

	G4VisManager *visManager = new G4VisExecutive();
	visManager->Initialize();

	G4UImanager *UImanager = G4UImanager::GetUIpointer();
	if (!ui) {
		// batch mode
		G4String command  = "/control/execute ";
		G4String fileName = argv[1];
		UImanager->ApplyCommand(command + fileName);
	} else {
		// interactive mode
		UImanager->ApplyCommand("/control/execute ../vis.mac");
		ui->SessionStart();
		delete ui;
	}
}
