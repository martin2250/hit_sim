#!/bin/python
import dataclasses
import sys
from typing import Optional
import numpy as np
import matplotlib.pyplot as plt
import subprocess
import pathlib
import glob
import os
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import json
import hashlib

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

geant4_env()

@dataclass
class Result:
    # from file
    position: np.ndarray
    momentum: np.ndarray
    energy: np.ndarray
    ptype: np.ndarray
    # computed
    angle_x: np.ma.masked_array

@dataclass
class Parameters:
    gap_width: float = 0.1
    gap_position: float = 0
    chip_thickness: float = 0.1
    backplate_thickness: float = 0.2
    particle_energy: float = 100
    particle_count: int = 40000
    detector_variant: str = 'chip_backplate_pcb'
    pcb_trace_fill: float = 0.5
    pcb_copper_thickness: float = 0.018

@dataclass
class Scene:
    parameters: Parameters
    result: Optional[Result] = None

    def get_filename(self) -> pathlib.Path:
        ph = hashlib.md5(json.dumps(dataclasses.asdict(self.parameters)).encode()).hexdigest()
        return pathlib.Path('/tmp/') / f'hit_sim_{ph}.txt'

    def load_result(self):
        res_dtype = 'f,f,f,f,f,f,f,S8'
        data = np.genfromtxt(self.get_filename(), dtype=res_dtype, unpack=True)
        position = data[0:3]
        momentum = data[3:6]
        energy = np.array(data[6])
        ptype = np.array(data[7])

        ok = (ptype == b'proton') & (energy > 5)

        angle_x = np.arctan2(momentum[0], momentum[2])

        self.result = Result(
            position=position,
            momentum=momentum,
            energy=np.ma.masked_array(energy, ~ok),
            ptype=ptype,
            angle_x=np.ma.masked_array(angle_x, ~ok),
        )

    def run(self):
        filename = self.get_filename()
        if filename.exists():
            print(f'using cached result for {filename}')
            return
        print(f'running sim for {filename}')
        parameters = [
            f'/hit_sim/set_gap_width {self.parameters.gap_width} mm',
            f'/hit_sim/set_chip_thickness {self.parameters.chip_thickness} mm',
            f'/hit_sim/set_particle_energy {self.parameters.particle_energy} MeV',
            f'/hit_sim/set_detector_variant {self.parameters.detector_variant}',
            f'/hit_sim/set_gap_position {self.parameters.gap_position:0.2f} mm',
            f'/hit_sim/set_backplate_thickness {self.parameters.backplate_thickness} mm',
            f'/hit_sim/set_pcb_trace_fill {self.parameters.pcb_trace_fill} mm',
            f'/hit_sim/set_pcb_copper_thickness {self.parameters.pcb_copper_thickness} mm',
            f'/hit_sim/file_open {filename}',
            f'/run/initialize',
            f'/run/beamOn {self.parameters.particle_count}',
            f'/hit_sim/file_close',
        ]
        mac_path = filename.with_suffix('.mac')
        with open(mac_path, 'w') as f:
            for param in parameters:
                print(param, file=f)
        hitsim_path = pathlib.Path(__file__).parent.parent
        retcode = subprocess.call([
            hitsim_path / 'release' / 'hit_sim',
            mac_path
        ], stdout=subprocess.DEVNULL)

        if retcode != 0:
            print('hit_sim: nonzero exit code!')
            exit(retcode)



scenes_offset: list[Scene] = []
for gap_position in np.linspace(0, 1, 3):
    scenes_offset.append(Scene(Parameters(
        gap_position = gap_position,
    )))

scene_water = Scene(Parameters(
    detector_variant='backplate_water',
    backplate_thickness=1,
))

scene_without_pcb = Scene(Parameters(
    detector_variant='backplate_chips',
))

scenes_trace_fill: list[Scene] = []
for pcb_trace_fill in [0.01, 0.3, 0.5, 0.99]:
    scenes_trace_fill.append(Scene(Parameters(
        pcb_trace_fill=pcb_trace_fill,
    )))


scenes_copper_thickness: list[Scene] = []
for copper_thickness in [1e-4, 7e-3, 18e-3, 35e-3]:
    scenes_copper_thickness.append(Scene(Parameters(
        pcb_copper_thickness=copper_thickness,
    )))

scenes: list[Scene] = []
scenes.extend(scenes_offset)
scenes.append(scene_water)
scenes.append(scene_without_pcb)
scenes.extend(scenes_trace_fill)
scenes.extend(scenes_copper_thickness)

print(scenes)

tpe = ThreadPoolExecutor(6)
_ = list(tpe.map(lambda s: s.run(), scenes))
_ = list(tpe.map(lambda s: s.load_result(), scenes))


def plot_scene_e_alpha(scene: Scene, ax_angle: plt.Axes, ax_momentum: plt.Axes, label: str):
    result = scene.result
    if not result:
        raise ValueError()
    momentum_range = (
        np.percentile(result.energy, 1) - 0.3,
        np.percentile(result.energy, 100) + 0.3,
    )
    momentum_range = (98.7, 100)
    angle_range = (
        1e3*np.percentile(result.angle_x, 0.5) - 5,
        1e3*np.percentile(result.angle_x, 99.5) + 5,
    )

    print(angle_range)
    print(result.angle_x)

    ax_angle.set_ylim(0, 2000)
    ax_momentum.set_ylim(0, 4000)

    ax_angle.hist(1e3*result.angle_x, range=angle_range,
                  bins=100, log=False, histtype='step', label=label)
    ax_momentum.hist(result.energy, range=momentum_range,
                     bins=100, log=False, histtype='step', label=label)

################################################################################

def plot_gap_positions():
    fig, (ax_angle, ax_momentum) = plt.subplots(1, 2, figsize=(9, 5))

    fig.suptitle('Compare Gap Positions')

    for scene in scenes_offset:
        plot_scene_e_alpha(scene, ax_angle, ax_momentum, f'gap offset {scene.parameters.gap_position} mm')

    ax_angle.set_xlabel('Deflection Angle X (mrad)')
    ax_momentum.set_xlabel('Remaining Energy (MeV)')

    ax_angle.legend(loc='lower left')

    fig.savefig('/tmp/gap_positions.png', dpi=150, transparent=True)

def plot_water_equivalent():
    fig, (ax_angle, ax_momentum) = plt.subplots(1, 2, figsize=(9, 5))

    fig.suptitle('Compare with simplified Stackups')

    plot_scene_e_alpha(scenes_offset[0], ax_angle, ax_momentum, f'full HitPix detector (gap)')
    plot_scene_e_alpha(scene_water, ax_angle, ax_momentum, f'1 mm water')
    plot_scene_e_alpha(scene_without_pcb, ax_angle, ax_momentum, f'without PCB')

    ax_angle.set_xlabel('Deflection Angle X (mrad)')
    ax_momentum.set_xlabel('Remaining Energy (MeV)')

    ax_angle.legend(loc='lower left')

    fig.savefig('/tmp/water_equivalent.png', dpi=150, transparent=True)

def plot_trace_fill():
    fig, (ax_angle, ax_momentum) = plt.subplots(1, 2, figsize=(9, 5))

    fig.suptitle('Compare Copper Fill Percentages')

    for scene in scenes_trace_fill:
        plot_scene_e_alpha(scene, ax_angle, ax_momentum, f'fill factor {scene.parameters.pcb_trace_fill}')

    ax_angle.set_xlabel('Deflection Angle X (mrad)')
    ax_momentum.set_xlabel('Remaining Energy (MeV)')

    ax_angle.legend(loc='lower left')

    fig.savefig('/tmp/trace_fill.png', dpi=150, transparent=True)

def plot_copper_thickness():
    fig, (ax_angle, ax_momentum) = plt.subplots(1, 2, figsize=(9, 5))

    fig.suptitle('Compare Copper Thickness')

    for scene in scenes_copper_thickness:
        plot_scene_e_alpha(scene, ax_angle, ax_momentum, f'{scene.parameters.pcb_copper_thickness*1e3} um copper')

    ax_angle.set_xlabel('Deflection Angle X (mrad)')
    ax_momentum.set_xlabel('Remaining Energy (MeV)')

    ax_angle.legend(loc='lower left')

    fig.savefig('/tmp/copper_thickness.png', dpi=150, transparent=True)

plot_gap_positions()
plot_water_equivalent()
plot_trace_fill()
plot_copper_thickness()


plt.show()
