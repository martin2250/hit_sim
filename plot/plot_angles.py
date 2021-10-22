#!/bin/python
import sys
import numpy as np
import matplotlib.pyplot as plt
import subprocess
import pathlib
import glob
import os
from concurrent.futures import ThreadPoolExecutor

def geant4_env():
    for filename in glob.glob('/etc/profile.d/geant4*.sh'):
        with open(filename) as file:
            for line in file:
                line = line.strip()
                if not line.startswith('export G4'):
                    continue
                line = line[len('export '):]
                name, _, value = line.partition('=')
                os.environ[name] = value


hitsim_path = pathlib.Path(__file__).parent.parent

beam_offsets = np.linspace(0, 1.2, 6)
file_names = [f'/tmp/offset_{o:0.2f}.dat' for o in beam_offsets]
runmac = '/tmp/run.mac'

def run_geant(args):
    beam_offset, file_name = args

    with open(file_name + '.mac', 'w') as f:
        print(f'/hit_sim/set_gap_width 1000 um', file=f)
        print(f'/hit_sim/set_gap_position {beam_offset:0.2f} mm', file=f)
        print(f'/hit_sim/set_particle_energy 120 MeV', file=f)

        print(f'/hit_sim/set_detector_variant backplate_water', file=f)
        # print(f'/hit_sim/set_detector_variant chip_backplate', file=f)

        print(f'/hit_sim/file_open {file_name}', file=f)
        print(f'/hit_sim/detector_update', file=f)
        print(f'/run/initialize', file=f)
        print(f'/run/beamOn 20000', file=f)
        print(f'/hit_sim/file_close', file=f)
    retcode = subprocess.call([
        hitsim_path / 'release' / 'hit_sim',
        file_name + '.mac'
    ])
    if retcode != 0:
        print('hit_sim: nonzero exit code!')
        exit(retcode)

if len(sys.argv) > 1 and sys.argv[1] == 'run':
    retcode = subprocess.call(['ninja'], cwd = hitsim_path / 'release')
    if retcode != 0:
        print('ninja: nonzero exit code!')
        exit(retcode)
    geant4_env()
    tpe = ThreadPoolExecutor(6)
    _ = list(tpe.map(run_geant, zip(beam_offsets, file_names)))

def plot_hist(fname: str, label: str):
    print(f'reading {fname}')
    data = np.genfromtxt(fname, dtype='f,f,f,f,f,f,S8', unpack=True)

    position = data[0:3]
    momentum = data[3:6]
    particle_type = np.array(data[6])

    momentum_transverse = np.square(momentum[0]) + np.square(momentum[1])
    # momentum_abs = np.sqrt(momentum_transverse + np.square(momentum[2]))
    momentum_transverse = np.sqrt(momentum_transverse)
    angle = np.arctan2(momentum_transverse, momentum[2])

    # electrons = particle_type == b'e-'
    protons = particle_type == b'proton'
    energy_left = momentum[2] > 5 # MeV

    protons_energy_left = protons & energy_left

    print(f'number of protons (with energy left): {np.sum(protons)} {np.sum(protons_energy_left)}')

    plt.hist(1e3*angle[protons_energy_left], range=(0,100), bins=100, log=True, histtype='step', label=label)
    # plt.hist(momentum[2][protons_energy_left], bins=100, log=True, histtype='step', label=label)

for beam_offset, file_name in zip(beam_offsets, file_names):
    plot_hist(file_name, f'offset {beam_offset:0.2} mm')
plt.xlabel('mrad')

plt.legend()
plt.savefig('/tmp/test.png')
plt.show()
