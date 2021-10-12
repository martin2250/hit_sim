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
    HitSimDetectorConstruction(){}
    virtual ~HitSimDetectorConstruction(){}

    virtual G4VPhysicalVolume* Construct() {
        G4NistManager* nist = G4NistManager::Instance();

        G4Material* mat_air = nist->FindOrBuildMaterial("G4_AIR");
        G4Material* mat_silicon = nist->FindOrBuildMaterial("G4_Si");

        G4Box* world_box = new G4Box("World", 10 * mm, 10 * mm, 50 * mm);
        G4LogicalVolume* world_logical =
            new G4LogicalVolume(world_box, mat_air, "World");
        G4VisAttributes* world_vis = new G4VisAttributes();
        world_vis->SetForceWireframe(true);
        world_logical->SetVisAttributes(world_vis);
        G4VPhysicalVolume* world_physical =
            new G4PVPlacement(nullptr,          // rotation
                              G4ThreeVector(),  // position
                              world_logical, "World", nullptr, false, 0, true);
        

        G4Box* chip_box = new G4Box("Chip", 5 * mm, 5 * mm, 200 * um);
            G4LogicalVolume* chip_logical =
            new G4LogicalVolume(chip_box, mat_silicon, "Chip");
        G4VPhysicalVolume* chip_physical =
            new G4PVPlacement(nullptr,          // rotation
                              G4ThreeVector(),  // position
                              chip_logical, "Chip", world_logical, false, 0, true);

        return world_physical;
    }
};

class HitSimPrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
   public:
    HitSimPrimaryGeneratorAction() {
        this->fParticleGun  = new G4ParticleGun(1);
        G4ParticleTable* particleTable = G4ParticleTable::GetParticleTable();
        G4ParticleDefinition* particle = particleTable->FindParticle("proton");
        this->fParticleGun->SetParticleDefinition(particle);
        this->fParticleGun->SetParticleEnergy(100*MeV);
        this->fParticleGun->SetParticlePosition(G4ThreeVector(0, 0, -20*mm));
        this->fParticleGun->SetParticleMomentumDirection(G4ThreeVector(0, 0, 1));
    }
    virtual ~HitSimPrimaryGeneratorAction() {
        delete this->fParticleGun;
    }

    virtual void GeneratePrimaries(G4Event* event) {
        this->fParticleGun->GeneratePrimaryVertex(event);
    }

    G4ParticleGun* fParticleGun;
};

class HitSimActionInitialization : public G4VUserActionInitialization {
   public:
    HitSimActionInitialization() {
    }
    virtual ~HitSimActionInitialization() {}

    virtual void Build() const {
        this->SetUserAction(new HitSimPrimaryGeneratorAction());
    }
    
    virtual void BuildForMaster() const {
    }
};

int main(int argc, char** argv) {
    G4UIExecutive* ui = 0;
    if (argc == 1) {
        ui = new G4UIExecutive(argc, argv);
    }

    auto* runManager =
        G4RunManagerFactory::CreateRunManager(G4RunManagerType::Default);

    runManager->SetUserInitialization(new HitSimDetectorConstruction());
    runManager->SetUserInitialization(new QBBC());
    runManager->SetUserInitialization(new HitSimActionInitialization());

    runManager->Initialize();

    G4VisManager* visManager = new G4VisExecutive();
    visManager->Initialize();

    G4UImanager* UImanager = G4UImanager::GetUIpointer();
    if (!ui) {
        // batch mode
        G4String command = "/control/execute ";
        G4String fileName = argv[1];
        UImanager->ApplyCommand(command + fileName);
    } else {
        // interactive mode
        UImanager->ApplyCommand("/control/execute ../vis.mac");
        ui->SessionStart();
        delete ui;
    }
}
