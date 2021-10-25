#!/bin/python
import sys
import numpy as np
import matplotlib.pyplot as plt
import subprocess
import pathlib
import glob
import os
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

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

@dataclass
class Scene:
    file_name: pathlib.Path
    label: str
    parameters: list[str]

scenes: list[Scene] = []

dir_top = pathlib.Path('/tmp/')

parameters_pre = [
    f'/hit_sim/set_gap_width 100 um',
    f'/hit_sim/set_particle_energy 50 MeV',
]

parameters_post = [
    f'/hit_sim/detector_update',
    f'/run/initialize',
    f'/run/beamOn 20000',
    f'/hit_sim/file_close',
]

for beam_offset in np.linspace(0, 1, 3):
    file_name = dir_top / f'offset_{beam_offset:0.2f}.dat'
    label = f'offset = {beam_offset:0.2f} mm'
    parameters = parameters_pre.copy() + [
        f'/hit_sim/set_detector_variant chip_backplate',
        f'/hit_sim/set_gap_position {beam_offset:0.2f} mm',
        f'/hit_sim/file_open {file_name}',
    ] + parameters_post.copy()
    scenes.append(Scene(
        file_name,
        label,
        parameters,
    ))

scenes.append(Scene(
    file_name = dir_top / f'water.dat',
    label = '0.35 mm water',
    parameters=parameters_pre.copy() + [
        f'/hit_sim/set_detector_variant backplate_water',
        f'/hit_sim/set_backplate_thickness 0.35 mm',
        f'/hit_sim/file_open {dir_top / "water.dat"}',
    ] + parameters_post.copy(),
))


hitsim_path = pathlib.Path(__file__).parent.parent

def run_geant(scene: Scene):
    mac_path = scene.file_name.with_suffix('.mac')

    with open(mac_path, 'w') as f:
        for param in scene.parameters:
            print(param, file=f)
       
    retcode = subprocess.call([
        hitsim_path / 'release' / 'hit_sim',
        mac_path
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
    _ = list(tpe.map(run_geant, scenes))


def plot_scene(scene: Scene, ax_angle: plt.Axes, ax_momentum: plt.Axes):
    print(f'reading {scene.file_name}')
    data = np.genfromtxt(scene.file_name, dtype='f,f,f,f,f,f,S8', unpack=True)

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

    ax_angle.hist(1e3*angle[protons_energy_left], range=(0,130), bins=100, log=True, histtype='step', label=scene.label)
    ax_momentum.hist(momentum[2][protons_energy_left], bins=100, log=True, histtype='step', label=scene.label)

fig, (ax_angle, ax_momentum) = plt.subplots(1, 2, figsize=(9, 5))

for scene in scenes:
    plot_scene(scene, ax_angle, ax_momentum)

ax_angle.set_xlabel('Deflection Angle (mrad)')
ax_momentum.set_xlabel('Remaining Energy (MeV)')

ax_momentum.legend(loc='upper left')

fig.savefig('/tmp/test.png', dpi=150, transparent=True)
plt.show()
