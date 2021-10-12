```
  //
  // materials for rad-source setup
  //
  //from http://www.physi.uni-heidelberg.de/~adler/TRD/TRDunterlagen/RadiatonLength/tgc2.htm
  //Epoxy (for FR4 )
  density = 1.2*g/cm3;
  G4Material* Epoxy = new G4Material("Epoxy" , density, ncomponents=2);
  Epoxy->AddElement(H, natoms=2);
  Epoxy->AddElement(C, natoms=2);

  //FR4 (Glass + Epoxy)
  density = 1.86*g/cm3;
  G4Material* FR4 = new G4Material("FR4"  , density, ncomponents=2);
  FR4->AddMaterial(SiO2, fractionmass=0.528);
  FR4->AddMaterial(Epoxy, fractionmass=0.472);
```