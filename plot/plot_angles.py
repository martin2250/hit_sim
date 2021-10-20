#!/bin/python
import numpy as np
import matplotlib.pyplot as plt

def plot_hist(fname: str):
    data_dtype = (np.float64,) * 6 + (str,)
    data = np.genfromtxt(fname, dtype=data_dtype, unpack=True)

    position = data[0:3]
    momentum = data[3:6]
    particle_type = data[6]

    transverse_momentum = np.sqrt(np.square(momentum[0]) + np.square(momentum[1]))
    angle = np.arctan2(transverse_momentum, momentum[2])

    plt.hist(1e3*angle, range=(0,10), bins=100, log=True, alpha=0.5)

plot_hist('/tmp/test.txt')
plot_hist('/tmp/test_chip.txt')
plot_hist('/tmp/test_air.txt')
plt.xlabel('mrad')
plt.show()
