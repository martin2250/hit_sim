#include "G4Box.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4ParticleGun.hh"
#include "G4PhysListFactory.hh"
#include "G4RadioactiveDecayPhysics.hh"
#include "G4RunManagerFactory.hh"
#include "G4StepLimiterPhysics.hh"
#include "G4UIExecutive.hh"
#include "G4UIcmdWithAString.hh"
#include "G4UImanager.hh"
#include "G4UImessenger.hh"
#include "G4UserLimits.hh"
#include "G4VUserActionInitialization.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4VisExecutive.hh"
#include "QBBC.hh"
#include <CLHEP/Random/RandGauss.h>
#include <G4UIcmdWithADoubleAndUnit.hh>
#include <G4UIcmdWithoutParameter.hh>

#include <iostream>
#include <mutex>

using namespace CLHEP;

double backplate_thickness = 200 * um;
double chip_thickness      = 150 * um;
double chip_gap            = 200 * um;
double chip_gap_offset     = 0 * mm;

// https://www.multi-circuit-boards.eu/en/pcb-design-aid/layer-buildup/flexible-pcb.html
double pcb_copper_thickness = 18 * um; // 2 * 18 um
double pcb_polyimide_thickness = 91 * um; // 25 + 2 * 13 + 2 * 20 (includes coverlay adhesive)
double pcb_trace_spacing = 400 * um;
double pcb_trace_fill = 0.5 * mm; // ignore mm

double particle_energy = 1 * MeV;

std::string detector_variant = "chip_backplate_pcb";

std::ofstream file_out;
std::mutex    file_out_mut;

class HitSimDetectorConstruction : public G4VUserDetectorConstruction {
  public:
	HitSimDetectorConstruction() {}
	virtual ~HitSimDetectorConstruction() {}

	virtual G4VPhysicalVolume *Construct() {
		G4NistManager *nist = G4NistManager::Instance();

		// G4Material *mat_air =
		//     nist->BuildMaterialWithNewDensity("G5_VACUUM", "G4_AIR", 0.1 * g / m3);
		G4Material *mat_air		= nist->FindOrBuildMaterial("G4_AIR");
		G4Material *mat_water = nist->FindOrBuildMaterial("G4_WATER");
		G4Material *mat_silicon = nist->FindOrBuildMaterial("G4_Si");
		G4Material *mat_carbon  = nist->FindOrBuildMaterial("G4_C");
		G4Material *mat_copper  = nist->FindOrBuildMaterial("G4_Cu");

		G4Element *el_carbon   = nist->FindOrBuildElement("C");
		G4Element *el_oxygen   = nist->FindOrBuildElement("O");
		G4Element *el_nitrogen = nist->FindOrBuildElement("N");
		G4Element *el_chlorine = nist->FindOrBuildElement("Cl");
		G4Element *el_hydrogen = nist->FindOrBuildElement("H");

		double world_width  = 20 * mm;
		double world_height = 20 * mm;
		double world_depth  = 20 * mm;

		// // epoxy elements C21H25ClO5
		// // (https://pubchem.ncbi.nlm.nih.gov/compound/Epoxy-resin)
		// G4Material *mat_epoxy = new G4Material("Epoxy", 1.1 * g / cm3, 4);
		// mat_epoxy->AddElementByNumberOfAtoms(el_carbon, 21);
		// mat_epoxy->AddElementByNumberOfAtoms(el_hydrogen, 25);
		// mat_epoxy->AddElementByNumberOfAtoms(el_chlorine, 1);
		// mat_epoxy->AddElementByNumberOfAtoms(el_oxygen, 5);
		// // CFRP with 50/50 mix
		// G4Material *mat_cfrp = new G4Material("CFRP", (1.1 + 2) / 2 * g / cm3, 2);
		// mat_cfrp->AddMaterial(mat_epoxy, 0.5);
		// mat_cfrp->AddMaterial(mat_carbon, 0.5);

		// from https://gemc.jlab.org/work/doxy/1.8/cpp__materials_8cc_source.html
		G4Material *mat_epoxy = new G4Material("Epoxy", 1.16 * g / cm3, 4);
		mat_epoxy->AddElement(el_hydrogen, 32); // Hydrogen
		mat_epoxy->AddElement(el_nitrogen, 2);  // Nitrogen
		mat_epoxy->AddElement(el_oxygen, 4);    // Oxygen
		mat_epoxy->AddElement(el_carbon, 15);   // Carbon
		G4Material *mat_cfrp = new G4Material("CFRP", 1.75 * g / cm3, 2);
		mat_cfrp->AddMaterial(mat_epoxy, 0.25);
		mat_cfrp->AddMaterial(mat_carbon, 0.75);

		G4Material* mat_kapton = new G4Material("Kapton", 1.413*g/cm3, 4);
		mat_kapton->AddElement(el_oxygen,5);
		mat_kapton->AddElement(el_carbon,22);
		mat_kapton->AddElement(el_nitrogen,2);
		mat_kapton->AddElement(el_hydrogen,10);

		// world (air filled box, wireframe render)
		G4Box *world_box = new G4Box("World", world_width / 2, world_height / 2, world_depth / 2);
		G4LogicalVolume *world_logical = new G4LogicalVolume(world_box, mat_air, "World");
		G4VisAttributes *world_vis     = new G4VisAttributes();
		world_vis->SetForceWireframe(true);
		world_logical->SetVisAttributes(world_vis);
		G4VPhysicalVolume *world_physical = new G4PVPlacement(
		    nullptr,         // rotation
		    G4ThreeVector(), // position
		    world_logical, "World", nullptr, false, 0, true);

		if (detector_variant.find("backplate") != std::string::npos) {
			// CFRP backplate
			G4Material *mat_backplate;
			if (detector_variant.find("water") != std::string::npos) {
				mat_backplate = nist->FindOrBuildMaterial("G4_WATER");
			} else {
				mat_backplate = mat_cfrp;
			}
			G4Box *backplate_box =
			    new G4Box("Backplate", world_width / 2, world_height / 2, backplate_thickness / 2);
			G4LogicalVolume *backplate_logical =
			    new G4LogicalVolume(backplate_box, mat_backplate, "Backplate");
			G4VPhysicalVolume *backplate_physical = new G4PVPlacement(
			    nullptr,                                      // rotation
			    G4ThreeVector(0, 0, backplate_thickness / 2), // position
			    backplate_logical, "Backplate", world_logical, false, 0, true);
		}

		if (detector_variant.find("chip") != std::string::npos) {
			// two chips with gap
			for (auto &sign_x : {-1.0, 1.0}) {
				auto chip_edge_inner = (sign_x * chip_gap / 2) + chip_gap_offset;
				auto chip_edge_outer = sign_x * world_width / 2;

				auto   chip_width = std::abs(chip_edge_inner - chip_edge_outer);
				auto   chip_pos   = (chip_edge_inner + chip_edge_outer) / 2;
				G4Box *chip_box =
				    new G4Box("Chip", chip_width / 2, world_height / 2, chip_thickness / 2);
				G4LogicalVolume *chip_logical = new G4LogicalVolume(chip_box, mat_silicon, "Chip");
				G4VPhysicalVolume *chip_physical = new G4PVPlacement(
				    nullptr, // rotation
				    G4ThreeVector(
				        chip_pos, 0, backplate_thickness + chip_thickness / 2), // position
				    chip_logical, "Chip", world_logical, false, 0, true);
			}
		}

		if (detector_variant.find("pcb") != std::string::npos) {
			std::vector<std::tuple<std::string, G4Material*, double, int>> layers {
				{"PCB Copper Top", mat_copper, pcb_copper_thickness, 1},
				{"PCB Kapton", mat_kapton, pcb_polyimide_thickness, 0},
				{"PCB Copper Bottom", mat_copper, pcb_copper_thickness, -1},
			};
			double depth = backplate_thickness + chip_thickness;

			for (auto &[name, mat, thickness, variant] : layers) {
				if (variant == 0) {
					G4cout << name << " placing full plate" << std::endl;
					// simple continuous layer
					G4Box *box =
						new G4Box(name, world_width / 2, world_height / 2, thickness / 2);
					G4LogicalVolume *logical =
						new G4LogicalVolume(box, mat, name);
					G4VisAttributes* attr = new G4VisAttributes( G4Colour(204.0/255.0, 106.0/255.0, 37.0/255.0, 1.) );
					logical->SetVisAttributes(attr);
					G4VPhysicalVolume *physical = new G4PVPlacement(
						nullptr,                                    // rotation
						G4ThreeVector(0, 0, depth + thickness / 2), // position
						logical, name, world_logical, false, 0, true);
				} else {
					// horizontal (1) or vertical (-1) stripes
					double trace_width = pcb_trace_spacing * pcb_trace_fill / mm; // pcb_trace_fill has units mm
					if (variant == 1) {
						G4Box *box = new G4Box(
							name, //
							world_width / 2,
							trace_width / 2,
							thickness / 2
						);
						G4LogicalVolume *logical = new G4LogicalVolume(box, mat, name);
						G4VisAttributes* attr = new G4VisAttributes( G4Colour(128.0/255.0, 53.0/255.0, 0.0) );
						logical->SetVisAttributes(attr);
						for (double pos = -(world_height / 2); (pos + trace_width) < (world_height / 2); pos += pcb_trace_spacing) {
							G4cout << name << " placing horizontal strip with height=" << trace_width << " at y=" << pos + (trace_width/2) << std::endl;
							G4VPhysicalVolume *physical = new G4PVPlacement(
								nullptr,                                    // rotation
								G4ThreeVector(0, pos + (trace_width/2), depth + thickness / 2), // position
								logical, name, world_logical, false, 0, true);
						}
					} else {
						G4Box *box = new G4Box(
							name, //
							trace_width / 2,
							world_height / 2,
							thickness / 2
						);
						G4LogicalVolume *logical = new G4LogicalVolume(box, mat, name);
						G4VisAttributes* attr = new G4VisAttributes( G4Colour(128.0/255.0, 53.0/255.0, 0.0) );
						logical->SetVisAttributes(attr);
						for (double pos = -(world_width / 2); (pos + trace_width) < (world_width / 2); pos += pcb_trace_spacing) {
							G4cout << name << " placing vertical strip with width=" << trace_width << " at x=" << pos + (trace_width/2) << std::endl;
							G4VPhysicalVolume *physical = new G4PVPlacement(
								nullptr,                                    // rotation
								G4ThreeVector(pos + (trace_width/2), 0, depth + thickness / 2), // position
								logical, name, world_logical, false, 0, true);
						}
					}
				}
				
				depth += thickness;
			}
		}

		return world_physical;
	}
};

class HitSimPrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
  public:
	HitSimPrimaryGeneratorAction() {
		this->fParticleGun                  = new G4ParticleGun(1);
		G4ParticleTable *     particleTable = G4ParticleTable::GetParticleTable();
		G4ParticleDefinition *particle      = particleTable->FindParticle("proton");
		this->fParticleGun->SetParticleDefinition(particle);
		this->fParticleGun->SetParticlePosition(G4ThreeVector(0 * mm, 0, -9 * mm));
		this->fParticleGun->SetParticleMomentumDirection(G4ThreeVector(0, 0, 1));
		this->fParticleGun->SetParticleEnergy(particle_energy);

		this->gauss = new CLHEP::RandGauss(CLHEP::RandGauss::getTheEngine());
	}
	virtual ~HitSimPrimaryGeneratorAction() { delete this->fParticleGun; }

	virtual void GeneratePrimaries(G4Event *event) {
		double pos_max = 6;
		double pos_x   = this->gauss->fire(0, 1 / 2.355);
		double pos_y   = this->gauss->fire(0, 1 / 2.355);
		pos_x          = std::min(std::max(pos_x, -pos_max), pos_max);
		pos_y          = std::min(std::max(pos_y, -pos_max), pos_max);
		fParticleGun->SetParticlePosition(G4ThreeVector(pos_x * mm, pos_y * mm, -9 * mm));
		this->fParticleGun->GeneratePrimaryVertex(event);
	}

	G4ParticleGun *   fParticleGun;
	CLHEP::RandGauss *gauss;
};

template <typename... Args> std::string string_format(const std::string &format, Args... args) {
	int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1; // Extra space for '\0'
	if (size_s <= 0) {
		throw std::runtime_error("Error during formatting.");
	}
	auto size = static_cast<size_t>(size_s);
	auto buf  = std::make_unique<char[]>(size);
	std::snprintf(buf.get(), size, format.c_str(), args...);
	return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

class HitSimTrackingAction : public G4UserTrackingAction {
  public:
	HitSimTrackingAction() {}

	virtual ~HitSimTrackingAction() {}

	virtual void PostUserTrackingAction(const G4Track *track) {
		std::string   particle_name     = track->GetParticleDefinition()->GetParticleName();
		G4ThreeVector particle_position = track->GetPosition();
		G4ThreeVector particle_momentum = track->GetMomentum();
		double        particle_energy   = track->GetKineticEnergy();

		if (particle_name != "proton") {
			return;
		}

		std::string line = string_format(
		    "%0.3f %0.3f %0.3f %0.3f %0.3f %0.3f %0.3f %s\n",
		    particle_position.getX() / mm,  // stop clang-format
		    particle_position.getY() / mm,  //
		    particle_position.getZ() / mm,  //
		    particle_momentum.getX() / MeV, //
		    particle_momentum.getY() / MeV, //
		    particle_momentum.getZ() / MeV, //
		    particle_energy / MeV,          //
		    particle_name.c_str()           //
		);

		{
			const std::lock_guard<std::mutex> lock(file_out_mut);
			if (!file_out.is_open()) {
				return;
			}
			file_out << line;
		}
	}
};

// class HitSimSteppingAction : public G4UserSteppingAction {
//   public:
// 	HitSimSteppingAction() {}
// 	virtual ~HitSimSteppingAction() {}

// 	virtual void UserSteppingAction(const G4Step *step) {
// 		G4cout << step->GetTrack()->GetVolume()->GetName() << std::endl;
// 	}
// };

class HitSimActionInitialization : public G4VUserActionInitialization {
  public:
	HitSimActionInitialization() {}
	virtual ~HitSimActionInitialization() {}

	virtual void Build() const {
		this->SetUserAction(new HitSimPrimaryGeneratorAction());
		this->SetUserAction(new HitSimTrackingAction());
		// this->SetUserAction(new HitSimSteppingAction());
	}

	virtual void BuildForMaster() const {}
};

class HitSimMessenger : public G4UImessenger {
	G4UIdirectory *          dir_hitsim;
	G4UIcmdWithoutParameter *cmd_detector_update;

	std::tuple<G4UIcmdWithADoubleAndUnit *, std::string, double *> double_params[9] = {
	    std::make_tuple(nullptr, "/hit_sim/set_gap_position", &chip_gap_offset),
	    std::make_tuple(nullptr, "/hit_sim/set_gap_width", &chip_gap),
	    std::make_tuple(nullptr, "/hit_sim/set_backplate_thickness", &backplate_thickness),
	    std::make_tuple(nullptr, "/hit_sim/set_chip_thickness", &chip_thickness),
	    std::make_tuple(nullptr, "/hit_sim/set_particle_energy", &particle_energy),
	    std::make_tuple(nullptr, "/hit_sim/set_pcb_copper_thickness", &pcb_copper_thickness),
	    std::make_tuple(nullptr, "/hit_sim/set_pcb_polyimide_thickness", &pcb_polyimide_thickness),
	    std::make_tuple(nullptr, "/hit_sim/set_pcb_trace_spacing", &pcb_trace_spacing),
	    std::make_tuple(nullptr, "/hit_sim/set_pcb_trace_fill", &pcb_trace_fill),
	};

	G4UIcmdWithAString *     cmd_file_open;
	G4UIcmdWithAString *     cmd_detector_variant;
	G4UIcmdWithoutParameter *cmd_file_close;

	HitSimDetectorConstruction *detector;

  public:
	HitSimMessenger(HitSimDetectorConstruction *_detector) : detector(_detector) {
		this->dir_hitsim = new G4UIdirectory("/hit_sim/");
		this->dir_hitsim->SetGuidance("custom hitsim stuff");

		for (auto &[cmd, name, ptr] : this->double_params) {
			cmd = new G4UIcmdWithADoubleAndUnit(name.c_str(), this);
			cmd->AvailableForStates(G4State_PreInit, G4State_Init, G4State_Idle);
		}

		this->cmd_detector_update = new G4UIcmdWithoutParameter("/hit_sim/detector_update", this);
		this->cmd_detector_update->AvailableForStates(G4State_PreInit, G4State_Init, G4State_Idle);

		this->cmd_file_open = new G4UIcmdWithAString("/hit_sim/file_open", this);
		this->cmd_file_open->SetParameterName("path", false);
		this->cmd_file_open->AvailableForStates(G4State_PreInit, G4State_Idle);

		this->cmd_detector_variant = new G4UIcmdWithAString("/hit_sim/set_detector_variant", this);
		this->cmd_detector_variant->SetParameterName("variant", false);
		this->cmd_detector_variant->AvailableForStates(G4State_PreInit, G4State_Idle);

		this->cmd_file_close = new G4UIcmdWithoutParameter("/hit_sim/file_close", this);
		this->cmd_file_close->AvailableForStates(G4State_Idle);
	}
	virtual ~HitSimMessenger() {}

	virtual void SetNewValue(G4UIcommand *new_cmd, G4String value) {
		if (new_cmd == this->cmd_detector_update) {
			G4RunManager::GetRunManager()->DefineWorldVolume(this->detector->Construct());
		} else if (new_cmd == this->cmd_file_open) {
			if (file_out.is_open()) {
				file_out.close();
			}
			file_out.open(value);
		} else if (new_cmd == this->cmd_file_close) {
			if (file_out.is_open()) {
				file_out.close();
			}
		} else if (new_cmd == this->cmd_detector_variant) {
			detector_variant = std::string(value);
		} else {
			for (auto &[cmd, name, ptr] : this->double_params) {
				if (cmd == new_cmd) {
					*ptr = cmd->GetNewDoubleValue(value);
					return;
				}
			}
		}
	}
};

int main(int argc, char **argv) {
	G4UIExecutive *ui = 0;
	if (argc == 1) {
		ui = new G4UIExecutive(argc, argv);
	}

	auto *runManager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Default);
	runManager->SetNumberOfThreads(2);

	auto detector           = new HitSimDetectorConstruction();
	auto detector_messenger = new HitSimMessenger(detector);

	runManager->SetUserInitialization(detector);
	// runManager->SetUserInitialization(new QBBC());
	G4PhysListFactory *    physListFactory = new G4PhysListFactory();
	G4VModularPhysicsList *physicsList     = physListFactory->GetReferencePhysList("QGSP_BERT_HP");
	physicsList->RegisterPhysics(new G4StepLimiterPhysics());
	physicsList->RegisterPhysics(new G4RadioactiveDecayPhysics());
	runManager->SetUserInitialization(physicsList);

	runManager->SetUserInitialization(new HitSimActionInitialization());

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
